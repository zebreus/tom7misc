
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <string>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <condition_variable>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "allocator.h"

#include "arcfour.h"
#include "threadutil.h"
#include "randutil.h"

#include "tetris.h"
#include "nes-tetris.h"
#include "movie-maker.h"
#include "encoding.h"
#include "viz/tetru-viz.h"
#include "hashing.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

using namespace std;
using uint8 = uint8_t;
using uint64 = uint64_t;
using int64 = int64_t;
  
static constexpr int MAX_CONCURRENT_EMUS = 16;

static constexpr const char *SOLFILE = "tetris/solutions.txt";
static constexpr const char *ROMFILE = "tetris/tetris.nes";

// Real blocks are very small: Each one corresponds to one
// Tetris game.
//
// 8 is very natural but this could probably be as high
// as 14, given that our plans all work on a board of
// depth 6 (and it might be possible to reduce that).
static constexpr int BLOCK_SIZE = 8;

struct CachingMovieMaker {

  std::optional<vector<uint8_t>> GetCached(
	  const vector<uint8_t> &pattern) {
	std::unique_lock<std::mutex> ul(m);
	auto it = cache.find(pattern);
	if (it == cache.end()) {
	  return {};
	} else {
	  return {it->second};
	}
  }

  void AddCached(const vector<uint8_t> &pattern,
				 const vector<uint8_t> &movie) {
	std::unique_lock<std::mutex> ul(m);
	cache[pattern] = movie;
  }
  
  std::mutex m;
  std::unordered_map<vector<uint8_t>, vector<uint8_t>,
					 Hashing<vector<uint8_t>>> cache;
};


static CachingMovieMaker *caching_movie_maker = nullptr;


struct Game {

  explicit Game(int seed) : seed(seed) {
	emu.reset(Emulator::Create(ROMFILE));
	CHECK(emu.get() != nullptr) << ROMFILE;
  }

  Emulator *GetEmu() {
	// Before Play(), or if from cache.
	if (emu.get() != nullptr)
	  return emu.get();
	
	CHECK(movie_maker.get() != nullptr);
	return movie_maker->GetEmu();
  }
  
  void Play(const vector<uint8> &bytes,
			MovieMaker::Callbacks callbacks) {
	// Do we have it in cache?
	std::optional<vector<uint8_t>> omovie =
	  caching_movie_maker->GetCached(bytes);
	if (omovie.has_value()) {
	  static constexpr int CACHED_PIECES = 25;
	  if (callbacks.game_start)
		callbacks.game_start(*emu, CACHED_PIECES);
	  for (int i = 0; i < omovie.value().size(); i++)
		uint8 b = omovie.value()[i];
		emu->StepFull(b, 0);
		// Call callback periodically too
		if (i % 60 == 0) {
		  if (callbacks.placed_piece) {
			callbacks.placed_piece(*emu, 1, CACHED_PIECES);
		  }
		}
	  }
	  if (callbacks.placed_piece)
		callbacks.placed_piece(*emu, CACHED_PIECES, CACHED_PIECES);
	} else {
	  movie_maker.reset(new MovieMaker(SOLFILE, std::move(emu), seed));
	  vector<uint8> movie = movie_maker->Play(bytes, callbacks);
	  caching_movie_maker->AddCached(bytes, movie);
	}
  }

  const int seed = 0;
  
  std::unique_ptr<Emulator> emu;
  std::unique_ptr<MovieMaker> movie_maker;
};


struct ThreadLimiter {
  
  void Flush() {
	{
	  std::unique_lock<std::mutex> ul(m);
	  const int old_epoch = epoch;
	  epoch++;
	  
	  cond.wait(ul, [this, old_epoch]{
		  for (int64 e : outstanding_writes)
			if (e <= old_epoch)
			  return false;
		  return true;
		});
	}
  }
  
  void Write(std::unique_ptr<std::function<void()>> f) {
	// Block (not returning) until we have recorded the
	// epoch and started the thread.
	int64 my_epoch = 0;
	{
	  std::unique_lock<std::mutex> ul(m);
	  cond.wait(ul, [this]{
		  return thread_budget > 0;
		});

	  thread_budget--;
	  my_epoch = epoch;
	  outstanding_writes.push_back(my_epoch);
	}

	// Now spawn the thread
	std::thread th([this, my_epoch](
		std::unique_ptr<std::function<void()>> f) {
		// run the passed-in code.
		(*f)();

		// Delete any resources from it.
		f.reset(nullptr);
		
		{
		  std::unique_lock<std::mutex> ul(m);
		  thread_budget++;
		  // Delete
		  for (int i = 0; i < (int)outstanding_writes.size(); i++) {
			if (outstanding_writes[i] == my_epoch) {
			  // swap 'n pop
			  if (i != (int)outstanding_writes.size() - 1) {
				outstanding_writes[i] =
				  outstanding_writes[outstanding_writes.size() - 1];
			  }
			  outstanding_writes.pop_back();
			  goto found;
			}
		  }
		  CHECK(false) << my_epoch << " was not in outstanding_writes?";
		found:;
		}
		cond.notify_all();
	  }, std::move(f));

	th.detach();
  }

private:
  std::mutex m;
  std::condition_variable cond;
  
  int thread_budget = MAX_CONCURRENT_EMUS;

  // Sequencing of writes and flushes. When a write begins,
  // it saves the current epoch to the outstanding writes.
  // A flush increments this value and then waits for all
  // outstanding writes to be >=.
  int64 epoch = 0;

  // contains epoch numbers.
  vector<int64> outstanding_writes;
  
};

static ThreadLimiter *thread_limiter = nullptr;

struct Block {
  Block(int id) : id(id) {}

  // Read the indicated portion of the block into the buffer.
  void Read(uint8_t *buf, int start, int count) {
	std::unique_lock<std::mutex> ul(mutex);
	outstanding_reads++;
	nbdkit_debug("TVIZ[o %d %d %d ]ZIVT",
				 id,
				 outstanding_reads, outstanding_writes);
	
	if (game.get() == nullptr) {
	  for (int i = 0; i < count; i++)
		buf[i] = "FORMATZ!"[i % 8];
	  outstanding_reads--;
	  nbdkit_debug("TVIZ[o %d %d %d ]ZIVT",
				   id,
				   outstanding_reads, outstanding_writes);
	  return;
	}

	cond.wait(ul, [this]{ return !busy; });

	ReadRaw(buf, start, count);
	
	outstanding_reads--;
	nbdkit_debug("TVIZ[o %d %d %d ]ZIVT",
				 id,
				 outstanding_reads, outstanding_writes);
  }

  // Game must exist.
  // Must hold lock.
  void ReadRaw(uint8_t *buf, int start, int count) {
	CHECK(game.get() != nullptr);
	
	Emulator *emu = game->GetEmu();
	vector<uint8_t> board = GetBoard(*emu);
	for (int i = 0; i < count; i++) {
	  int byte_idx = start + i;
	  CHECK(byte_idx >= 0 && byte_idx < BLOCK_SIZE) <<
		start << " " << count;
	  // counting from the bottom
	  int pos = 10 * (19 - byte_idx) + 2;
	  uint8 byte = 0;
	  for (int b = 0; b < BLOCK_SIZE; b++) {
		byte <<= 1;
		// 0xEF = empty
		byte |= ((board[pos] != 0xEF) ? 0b1 : 0b0);
	  }
	  buf[i] = byte;
	}
  }
  
  // Write the buffer to the indicated portion of the block.
  void Write(const uint8_t *buf, int start, int count) {
	// Make sure we don't spawn the write until it has exclusive
	// access, as we don't want an unscheduled thread blocking a
	// scheduled one forever.
	{
	  std::unique_lock<std::mutex> ul(mutex);
	  outstanding_writes++;
	  nbdkit_debug("TVIZ[o %d %d %d ]ZIVT",
				   id,
				   outstanding_reads, outstanding_writes);

	  // PERF we could terminate an in-progress write.
			
	  cond.wait(ul, [this]{ return !busy; });
			
	  // Claim exclusive access.
	  busy = true;

	  nbdkit_debug("create game for %d", id);
	  
	  game.reset(new Game(id));
	}

	auto uf = std::make_unique<std::function<void()>>(
		[this, buf, start, count]() {
		  // Pattern to write. We need to start by reading if
		  // this is not the complete block.
		  vector<uint8_t> pattern(BLOCK_SIZE, 0);
		  CHECK(start >= 0 && start + count <= BLOCK_SIZE);
		  if (start != 0 || count != BLOCK_SIZE) {
			nbdkit_debug("need prev read for %d", id);	  
			// Might as well read the whole thing!
			ReadRaw(pattern.data(), 0, BLOCK_SIZE);
		  }
		  for (int i = 0; i < count; i++) {
			pattern[start + i] = buf[i];
		  }

		  // TODO: Visualize in callbacks.
		  MovieMaker::Callbacks callbacks;
		  callbacks.placed_piece = [this](const Emulator &emu,
										  int pieces_done,
										  int total_pieces) {
			  vector<uint8_t> board = GetBoard(emu);
			  if (!IsLineClearing(emu)) {
				Shape shape = (Shape)emu.ReadRAM(MEM_CURRENT_PIECE);
				int x = emu.ReadRAM(MEM_CURRENT_X) - ShapeXOffset(shape);
				// XXX adjust for NES offsets!
				int y = emu.ReadRAM(MEM_CURRENT_Y);
				DrawShapeOnBoard(0xFF, shape, x, y, &board);
			  }

			  const std::string encoded_board =
				BoardPic::ToString(GetBoard(emu));
			  std::unique_lock<std::mutex> ul(mutex);
			  /*
				printf("block %d: %d/%d pieces done\n",
				id, pieces_done, total_pieces);
			  */
			  nbdkit_debug("TVIZ[o %d %d %d %s]ZIVT",
						   id,
						   outstanding_reads, outstanding_writes,
						   encoded_board.c_str());
			};

		  // write from bottom to top.
		  game->Play(pattern, callbacks);

		  nbdkit_debug("finished playing %d", id);

		  {
			std::unique_lock<std::mutex> ul(mutex);
			// We should have the exclusive lock. Release it.
			CHECK(busy);
			busy = false;
			outstanding_writes--;
			nbdkit_debug("TVIZ[o %d %d %d ]ZIVT",
						 id,
						 outstanding_reads, outstanding_writes);
		  }

		  cond.notify_all();
		});

	thread_limiter->Write(std::move(uf));
  }

  const int id = 0;

private:

  std::mutex mutex;
  std::condition_variable cond;

  // Can be null, which means no data have been written yet.
  std::unique_ptr<Game> game;
  // If true, a write is in progress.
  bool busy = false;
  int outstanding_reads = 0, outstanding_writes = 0;
};

// Test version of buffer where we actually keep the backing
// store, but only provide access to it slowly.
struct Blocks {
  explicit Blocks(int64_t num_blocks) : num_blocks(num_blocks) {
	for (int i = 0; i < num_blocks; i++)
	  mem.push_back(new Block(i));
  }

  // Read, into the beginning of buf, 'count' bytes from the block,
  // starting at 'start'.
  inline void Read(int block_idx, uint8_t *buf, int start, int count) {
	// nbdkit_debug("Read %d[%d,%d] -> %p\n", block_idx, start, count, buf);
	nbdkit_debug("TVIZ[r %d]ZIVT", block_idx);
	mem[block_idx]->Read(buf, start, count);
  }

  inline void Write(int block_idx, const uint8_t *buf, int start, int count) {
	nbdkit_debug("Write %d[%d,%d] <- %p\n", block_idx, start, count, buf);
	nbdkit_debug("TVIZ[w %d]ZIVT", block_idx);
	mem[block_idx]->Write(buf, start, count);
  }

  ~Blocks() {
	for (Block *b : mem) delete b;
	mem.clear();
  }

  const int64_t num_blocks = 0;
  vector<Block *> mem;
};

static int64_t num_blocks = -1;
// Allocated buffer.
static Blocks *blocks = nullptr;

static int tetru_after_fork(void) {
  nbdkit_debug("After fork! num_blocks %lu\n", num_blocks);
  caching_movie_maker = new CachingMovieMaker();
  thread_limiter = new ThreadLimiter();
  blocks = new Blocks(num_blocks);
  return 0;
}

static void tetru_unload(void) {
  delete blocks;
  blocks = nullptr;
  delete thread_limiter;
  thread_limiter = nullptr;
  delete caching_movie_maker;
  caching_movie_maker = nullptr;
}

static int tetru_config(const char *key, const char *value) {
  if (strcmp(key, "num_bytes") == 0) {
    int64_t num_bytes = nbdkit_parse_size(value);
    if (num_bytes == -1)
      return -1;
	if (num_bytes % BLOCK_SIZE != 0) {
	  nbdkit_error("bytes must be divisible by block size, %d", BLOCK_SIZE);
	  return -1;
	}

	num_blocks = num_bytes / BLOCK_SIZE;
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int tetru_config_complete(void) {
  if (num_blocks == -1) {
    nbdkit_error("you must specify num_bytes=<NUM> on the command line");
    return -1;
  }
  return 0;
}

#define tetru_config_help \
  "num_bytes=<NUM>  (required) Number of bytes in the backing buffer"

static void tetru_dump_plugin(void) {
}

[[maybe_unused]]
static int tetru_get_ready(void) {
  // ?
  return 0;
}

/* Create the per-connection handle. */
static void *tetru_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t tetru_get_size(void *handle) {
  return num_blocks * BLOCK_SIZE;
}

/* Flush is a no-op, so advertise native FUA support */
static int tetru_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int tetru_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int tetru_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
// read count bytes starting at offset into buf
static int tetru_pread(void *handle, void *buf_void,
					   uint32_t count, uint64_t offset,
					   uint32_t flags) {
  uint8_t *buf = (uint8_t*)buf_void;
  // nbdkit_debug("pread(%u, %lu)\n", count, offset);
  assert(!flags);
  
  // one block at a time
  for (;;) {
	if (count == 0)
	  return 0;

	// Read from the first block in the range
	//
	// [---------------][--------------...
	//         ^
	// [-skip--|-rest--]
	int block_idx = offset / BLOCK_SIZE;
	int skip = offset % BLOCK_SIZE;
	int rest = BLOCK_SIZE - skip;
	// But don't read beyond requested count
	int bytes_to_read = std::min(rest, (int)count);

	blocks->Read(block_idx, buf, skip, bytes_to_read);
	// Prepare for next iteration
	count -= bytes_to_read;
	buf += bytes_to_read;
	offset += bytes_to_read;
  }
}

/* Write data. */
static int tetru_pwrite(void *handle, const void *buf_void,
						uint32_t count, uint64_t offset,
						uint32_t flags) {
  const uint8_t *buf = (const uint8_t*)buf_void;
  // nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);

  // Just as in the read case.
  for (;;) {
	if (count == 0)
	  return 0;

	int block_idx = offset / BLOCK_SIZE;
	int skip = offset % BLOCK_SIZE;
	int rest = BLOCK_SIZE - skip;
	int bytes_to_read = std::min(rest, (int)count);

	blocks->Write(block_idx, buf, skip, bytes_to_read);
	// Prepare for next iteration
	count -= bytes_to_read;
	buf += bytes_to_read;
	offset += bytes_to_read;
  }
}

static int tetru_flush(void *handle, uint32_t flags) {
  thread_limiter->Flush();
  return 0;
}

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "tetru",
  .version           = "1.0.0",
  .load              = nullptr,
  .unload            = tetru_unload,
  .config            = tetru_config,
  .config_complete   = tetru_config_complete,
  .config_help       = tetru_config_help,

  .open              = tetru_open,
  .get_size          = tetru_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  // well, you can rotate the tetris pieces? :)
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = tetru_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = tetru_can_fua,

  .pread             = tetru_pread,
  .pwrite            = tetru_pwrite,
  .flush             = tetru_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_bytes",
  .can_multi_conn    = tetru_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = tetru_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // tetru_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = nullptr,
  .after_fork        = tetru_after_fork,

};

NBDKIT_REGISTER_PLUGIN(plugin)
