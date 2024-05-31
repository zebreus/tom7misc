
#include <string>
#include <vector>

#include "image.h"


// Automatic image tracing.
struct Animation {

  struct Options {
    // Amount to smooth each layer of color.
    int smooth_passes = 5;

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

    // This increases the smoothness of strokes by subdividing each
    // sub-frame into smaller timesteps (but only applying a fraction of
    // the velocity).
    int timesteps_per_frame = 8;

    // Number of sub-frames per actual image frame. They get
    // averaged together.
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
