// Copyright 2018 The MediaPipe Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mediapipe/calculators/image/scale_image_utils.h"

#include "mediapipe/framework/port/gmock.h"
#include "mediapipe/framework/port/gtest.h"
#include "mediapipe/framework/port/status_matchers.h"

namespace mediapipe {
namespace scale_image {
namespace {

TEST(ScaleImageUtilsTest, FindCropDimensions) {
  int crop_width;
  int crop_height;
  int col_start;
  int row_start;
  // No cropping because aspect ratios should be ignored.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(50, 100, "0/1", "1/0", &crop_width,
                                         &crop_height, &col_start, &row_start));
  EXPECT_EQ(50, crop_width);
  EXPECT_EQ(100, crop_height);
  EXPECT_EQ(0, row_start);
  EXPECT_EQ(0, col_start);

  // Tests proto examples.
  // 16:9 aspect ratio, should be unchanged.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(1920, 1080, "9/16", "16/9",
                                         &crop_width, &crop_height, &col_start,
                                         &row_start));
  EXPECT_EQ(0, col_start);
  EXPECT_EQ(1920, crop_width);
  EXPECT_EQ(0, row_start);
  EXPECT_EQ(1080, crop_height);
  // 10:16 aspect ratio, should be unchanged.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(640, 1024, "9/16", "16/9", &crop_width,
                                         &crop_height, &col_start, &row_start));
  EXPECT_EQ(0, col_start);
  EXPECT_EQ(640, crop_width);
  EXPECT_EQ(0, row_start);
  EXPECT_EQ(1024, crop_height);

  // 2:1 aspect ratio, width is cropped.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(640, 320, "9/16", "16/9", &crop_width,
                                         &crop_height, &col_start, &row_start));
  EXPECT_EQ(36, col_start);
  EXPECT_EQ(568, crop_width);
  EXPECT_EQ(0, row_start);
  EXPECT_EQ(320, crop_height);
  // 1:5 aspect ratio, height is cropped.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(96, 480, "9/16", "16/9", &crop_width,
                                         &crop_height, &col_start, &row_start));
  EXPECT_EQ(0, col_start);
  EXPECT_EQ(96, crop_width);
  EXPECT_EQ(155, row_start);
  EXPECT_EQ(170, crop_height);

  // Tests min = max, crops width.
  MEDIAPIPE_ASSERT_OK(FindCropDimensions(200, 100, "1/1", "1/1", &crop_width,
                                         &crop_height, &col_start, &row_start));
  EXPECT_EQ(50, col_start);
  EXPECT_EQ(100, crop_width);
  EXPECT_EQ(0, row_start);
  EXPECT_EQ(100, crop_height);
}

TEST(ScaleImageUtilsTest, FindOutputDimensionsPreserveRatio) {
  int output_width;
  int output_height;
  // Not scale.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, -1, -1, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(200, output_width);
  EXPECT_EQ(100, output_height);
  // Not scale with odd input size.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(201, 101, -1, -1, false, false,
                                           &output_width, &output_height));
  EXPECT_EQ(201, output_width);
  EXPECT_EQ(101, output_height);
  // Scale down by 1/2.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 100, -1, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(100, output_width);
  EXPECT_EQ(50, output_height);
  // Scale up, doubling dimensions.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, -1, 200, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(400, output_width);
  EXPECT_EQ(200, output_height);
  // Fits a 2:1 image into a 150 x 150 box. Output dimensions are always
  // visible by 2.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 150, 150, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(150, output_width);
  EXPECT_EQ(74, output_height);
  // Fits a 2:1 image into a 400 x 50 box.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 400, 50, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(100, output_width);
  EXPECT_EQ(50, output_height);
  // Scale to multiple number with odd targe size.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 101, -1, true, true,
                                           &output_width, &output_height));
  EXPECT_EQ(100, output_width);
  EXPECT_EQ(50, output_height);
  // Scale to multiple number with odd targe size.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 101, -1, true, false,
                                           &output_width, &output_height));
  EXPECT_EQ(100, output_width);
  EXPECT_EQ(50, output_height);
  // Scale to odd size.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 151, 101, false, false,
                                           &output_width, &output_height));
  EXPECT_EQ(151, output_width);
  EXPECT_EQ(101, output_height);
}

// Tests scaling without keeping the aspect ratio fixed.
TEST(ScaleImageUtilsTest, FindOutputDimensionsNoAspectRatio) {
  int output_width;
  int output_height;
  // Scale width only.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 100, -1, false, true,
                                           &output_width, &output_height));
  EXPECT_EQ(100, output_width);
  EXPECT_EQ(100, output_height);
  // Scale height only.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, -1, 200, false, true,
                                           &output_width, &output_height));
  EXPECT_EQ(200, output_width);
  EXPECT_EQ(200, output_height);
  // Scale both dimensions.
  MEDIAPIPE_ASSERT_OK(FindOutputDimensions(200, 100, 150, 200, false, true,
                                           &output_width, &output_height));
  EXPECT_EQ(150, output_width);
  EXPECT_EQ(200, output_height);
}

}  // namespace
}  // namespace scale_image
}  // namespace mediapipe
