
#include <vector>

#include "image.h"


// Automatic image tracing.
struct Animation {

  // You may need to tweak options to get good results. The
  // defaults are decent choices for my style of artwork on
  // an image for 1920x1080 resolution, which produces
  // about ~30 seconds of frames at 60fps for a portrait.
  struct Options {
    // Amount to smooth each layer of color.
    int smooth_passes = 5;
    // In [0, 1]. Lower values cause smoothing to happen more quickly.
    float smooth_vote_threshold = 0.5f;

    // The layer's pen size is determined automatically (based on
    // the diameter of color regions) but this sets the absolute
    // limits. In pixels.
    float max_pen_radius = 14.0f;
    float min_pen_radius = 2.0f;

    // How fast we draw, in pixels per (sub-)frame.
    float max_pen_velocity = 24.0f;

    // In [0, 1]. How much of the target velocity gets sent
    // to the current velocity per (sub-)frame (this is not how
    // physics works).
    float pen_acceleration = 0.5f;

    // When we choose what region a pixel belongs to, the primary
    // signal is the perceptual color difference to selected layer
    // colors. But it will often look better to attach it to an
    // adjacent region if the color is close enough, which is common
    // on anti-aliased input images. We consider the color to be
    // close enough to an adjacent region if its Delta-E does not
    // exceed this threshold.
    float adjacent_deltae_threshold = 10.0f;

    // Unimplemented.
    // If a connected component has no more than than this many pixels,
    // consider it ambiguous and possibly join it to an adjacent
    // region. In combination with above, this can prevent ugly-looking
    // mattes even if matte pixel values exactly match a selected
    // layer color.
    int max_fragile_piece_size = 10;

    // This increases the smoothness of strokes by subdividing each
    // sub-frame into smaller timesteps (but only applying a fraction of
    // the velocity).
    int timesteps_per_frame = 8;

    // Number of sub-frames per actual image frame. They get
    // averaged together. Increasing this is the simplest way to
    // get fewer frames in the output.
    int blend_frames = 20;

    // 0 = no output
    int verbosity = 1;
  };

  // This keeps a reference to the image, which must outlive
  // the Animation object (and not be modified).
  static Animation *Create(const ImageRGBA &image_in,
                           const Options &options);
  virtual ~Animation();

  virtual std::vector<ImageRGBA> Animate() = 0;

  // After animating. For debugging.
  virtual const ImageRGBA &GetPoster() const = 0;

 protected:
  // Use Create.
  Animation();
};
