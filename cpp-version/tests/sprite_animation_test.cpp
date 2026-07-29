#include <gtest/gtest.h>
#include "rendering/sprite_animation.h"

// A 4x1 sheet like asmd4x1.png (512x128 = four 128x128 frames), 10 fps.
static SpriteAnimation makeAsmd() {
    return SpriteAnimation{TEX_ASMD, /*columns*/ 4, /*rows*/ 1, /*fps*/ 10.0f};
}

TEST(SpriteAnimation, FrameIndexStepsAndWraps) {
    SpriteAnimation a = makeAsmd();
    EXPECT_EQ(a.frameCount(), 4);

    // At 10 fps each frame is 0.1s. Frame index = floor(age*fps) % count.
    EXPECT_EQ(a.frameIndexAt(0.00f), 0);
    EXPECT_EQ(a.frameIndexAt(0.05f), 0);
    EXPECT_EQ(a.frameIndexAt(0.10f), 1);
    EXPECT_EQ(a.frameIndexAt(0.25f), 2);
    EXPECT_EQ(a.frameIndexAt(0.35f), 3);
    EXPECT_EQ(a.frameIndexAt(0.40f), 0);   // wraps after 4 frames
    EXPECT_EQ(a.frameIndexAt(0.45f), 0);
}

TEST(SpriteAnimation, SourceRectSelectsColumn) {
    SpriteAnimation a = makeAsmd();
    const int W = 512, H = 128;  // 4 frames of 128x128

    Rectangle r0 = a.sourceRect(0.00f, W, H);   // frame 0
    EXPECT_FLOAT_EQ(r0.x, 0.0f);
    EXPECT_FLOAT_EQ(r0.y, 0.0f);
    EXPECT_FLOAT_EQ(r0.width, 128.0f);
    EXPECT_FLOAT_EQ(r0.height, 128.0f);

    Rectangle r2 = a.sourceRect(0.25f, W, H);   // frame 2 → third column
    EXPECT_FLOAT_EQ(r2.x, 256.0f);
    EXPECT_FLOAT_EQ(r2.y, 0.0f);
    EXPECT_FLOAT_EQ(r2.width, 128.0f);
    EXPECT_FLOAT_EQ(r2.height, 128.0f);
}

TEST(SpriteAnimation, SingleFrameIsStatic) {
    SpriteAnimation a{TEX_FLARE, 1, 1, 10.0f};
    EXPECT_EQ(a.frameCount(), 1);
    EXPECT_EQ(a.frameIndexAt(5.0f), 0);
    Rectangle r = a.sourceRect(5.0f, 32, 32);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.width, 32.0f);
}
