// Copyright 2019 The MediaPipe Authors.
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

#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "mediapipe/calculators/image/opencv_image_encoder_calculator.pb.h"
#include "mediapipe/calculators/tensorflow/pack_media_sequence_calculator.pb.h"
#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/formats/detection.pb.h"
#include "mediapipe/framework/formats/location.h"
#include "mediapipe/framework/port/canonical_errors.h"
#include "mediapipe/framework/port/opencv_imgcodecs_inc.h"
#include "mediapipe/framework/port/ret_check.h"
#include "mediapipe/framework/port/status.h"
#include "mediapipe/util/sequence/media_sequence.h"
#include "mediapipe/util/sequence/media_sequence_util.h"
#include "tensorflow/core/example/example.pb.h"
#include "tensorflow/core/example/feature.pb.h"

namespace mediapipe {

const char kSequenceExampleTag[] = "SEQUENCE_EXAMPLE";
const char kImageTag[] = "IMAGE";
const char kFloatFeaturePrefixTag[] = "FLOAT_FEATURE_";
const char kForwardFlowEncodedTag[] = "FORWARD_FLOW_ENCODED";
const char kBBoxTag[] = "BBOX";
const char kSegmentationMaskTag[] = "CLASS_SEGMENTATION";

namespace tf = ::tensorflow;
namespace mpms = ::mediapipe::mediasequence;

// Sink calculator to package streams into tf.SequenceExamples.
//
// The calculator takes a tf.SequenceExample as a side input and then adds
// the data from inputs to the SequenceExample with timestamps. Additional
// context features can be supplied verbatim in the calculator's options. The
// SequenceExample will conform to the description in media_sequence.h.
//
// The supported input stream tags are "IMAGE", which stores the encoded
// images from the OpenCVImageEncoderCalculator, "FORWARD_FLOW_ENCODED", which
// stores the encoded optical flow from the same calculator, "BBOX" which stores
// bounding boxes from vector<Detections>, and streams with the
// "FLOAT_FEATURE_${NAME}" pattern, which stores the values from vector<float>'s
// associated with the name ${NAME}. Audio streams (i.e. Matrix with a
// TimeSeriesHeader) are given extra packing and unpacking support and are named
// similar to floats with the pattern "AUDIO_${NAME}". "IMAGE_${NAME}" and
// "BBOX_${NAME}" will also store prefixed versions of each stream, which allows
// for multiple image streams to be included. However, the default names are
// suppored by more tools. "ENCODED_MEDIA" stores a video encoding for the clip
// directly. The last packet on this stream is stored, and can be unpacked with
// the metadata generator. Because the media decoder always starts with
// timestamp zero, the "ENCODED_MEDIA_START_TIMESTAMP" should be recorded as
// well. Use the FirstTimestampCalculator to determine this value.
//
// Example config:
// node {
//   calculator: "PackMediaSequenceCalculator"
//   input_side_packet: "SEQUENCE_EXAMPLE:example_input_side_packet"
//   input_stream: "IMAGE:frames"
//   input_stream: "FLOAT_FEATURE_FDENSE:fdense_vf"
//   output_stream: "SEQUENCE_EXAMPLE:example_output_stream"
//   options {
//     [mediapipe.PackMediaSequenceCalculatorOptions.ext]: {
//       context_feature_map {
//         feature {
//           key: "image/frames_per_second"
//           value {
//             float_list {
//               value: 30.0
//             }
//           }
//         }
//       }
//     }
//   }
// }
namespace {
uint8 ConvertFloatToByte(const float float_value) {
  float clamped_value = MathUtil::Clamp(0.0f, 1.0f, float_value);
  return static_cast<uint8>(clamped_value * 255.0 + .5f);
}
}  // namespace

class PackMediaSequenceCalculator : public CalculatorBase {
 public:
  static ::mediapipe::Status GetContract(CalculatorContract* cc) {
    RET_CHECK(cc->InputSidePackets().HasTag(kSequenceExampleTag));
    cc->InputSidePackets().Tag(kSequenceExampleTag).Set<tf::SequenceExample>();

    if (cc->Inputs().HasTag(kForwardFlowEncodedTag)) {
      cc->Inputs()
          .Tag(kForwardFlowEncodedTag)
          .Set<OpenCvImageEncoderCalculatorResults>();
    }
    if (cc->Inputs().HasTag(kSegmentationMaskTag)) {
      cc->Inputs().Tag(kSegmentationMaskTag).Set<std::vector<Detection>>();
    }

    for (const auto& tag : cc->Inputs().GetTags()) {
      if (absl::StartsWith(tag, kImageTag)) {
        std::string key = "";
        if (tag != kImageTag) {
          int tag_length = sizeof(kImageTag) / sizeof(*kImageTag) - 1;
          if (tag[tag_length] == '_') {
            key = tag.substr(tag_length + 1);
          } else {
            continue;  // Skip keys that don't match "(kImageTag)_?"
          }
        }
        cc->Inputs().Tag(tag).Set<OpenCvImageEncoderCalculatorResults>();
      }
      if (absl::StartsWith(tag, kBBoxTag)) {
        std::string key = "";
        if (tag != kBBoxTag) {
          int tag_length = sizeof(kBBoxTag) / sizeof(*kBBoxTag) - 1;
          if (tag[tag_length] == '_') {
            key = tag.substr(tag_length + 1);
          } else {
            continue;  // Skip keys that don't match "(kBBoxTag)_?"
          }
        }
        cc->Inputs().Tag(tag).Set<std::vector<Detection>>();
      }
      if (absl::StartsWith(tag, kFloatFeaturePrefixTag)) {
        cc->Inputs().Tag(tag).Set<std::vector<float>>();
      }
    }

    CHECK(cc->Outputs().HasTag(kSequenceExampleTag) ||
          cc->OutputSidePackets().HasTag(kSequenceExampleTag))
        << "Neither the output stream nor the output side packet is set to "
           "output the sequence example.";
    if (cc->Outputs().HasTag(kSequenceExampleTag)) {
      cc->Outputs().Tag(kSequenceExampleTag).Set<tf::SequenceExample>();
    }
    if (cc->OutputSidePackets().HasTag(kSequenceExampleTag)) {
      cc->OutputSidePackets()
          .Tag(kSequenceExampleTag)
          .Set<tf::SequenceExample>();
    }
    return ::mediapipe::OkStatus();
  }

  ::mediapipe::Status Open(CalculatorContext* cc) override {
    sequence_ = ::absl::make_unique<tf::SequenceExample>(
        cc->InputSidePackets()
            .Tag(kSequenceExampleTag)
            .Get<tf::SequenceExample>());

    const auto& context_features =
        cc->Options<PackMediaSequenceCalculatorOptions>().context_feature_map();
    for (const auto& feature : context_features.feature()) {
      *mpms::MutableContext(feature.first, sequence_.get()) = feature.second;
    }
    for (const auto& tag : cc->Inputs().GetTags()) {
      features_present_[tag] = false;
    }

    if (cc->Options()
            .GetExtension(PackMediaSequenceCalculatorOptions::ext)
            .replace_data_instead_of_append()) {
      for (const auto& tag : cc->Inputs().GetTags()) {
        if (absl::StartsWith(tag, kImageTag)) {
          std::string key = "";
          if (tag != kImageTag) {
            int tag_length = sizeof(kImageTag) / sizeof(*kImageTag) - 1;
            if (tag[tag_length] == '_') {
              key = tag.substr(tag_length + 1);
            } else {
              continue;  // Skip keys that don't match "(kImageTag)_?"
            }
          }
          mpms::ClearImageEncoded(key, sequence_.get());
          mpms::ClearImageTimestamp(key, sequence_.get());
        }
      }
      if (cc->Inputs().HasTag(kForwardFlowEncodedTag)) {
        mpms::ClearForwardFlowEncoded(sequence_.get());
        mpms::ClearForwardFlowTimestamp(sequence_.get());
      }

      for (const auto& tag : cc->Inputs().GetTags()) {
        if (absl::StartsWith(tag, kFloatFeaturePrefixTag)) {
          std::string key = tag.substr(sizeof(kFloatFeaturePrefixTag) /
                                           sizeof(*kFloatFeaturePrefixTag) -
                                       1);
          mpms::ClearFeatureFloats(key, sequence_.get());
          mpms::ClearFeatureTimestamp(key, sequence_.get());
        }
      }
    }

    if (cc->Outputs().HasTag(kSequenceExampleTag)) {
      cc->Outputs()
          .Tag(kSequenceExampleTag)
          .SetNextTimestampBound(Timestamp::Max());
    }
    return ::mediapipe::OkStatus();
  }

  ::mediapipe::Status VerifySequence() {
    std::string error_msg = "Missing features - ";
    bool all_present = true;
    for (auto iter : features_present_) {
      if (!iter.second) {
        all_present = false;
        absl::StrAppend(&error_msg, iter.first, ", ");
      }
    }
    if (all_present) {
      return ::mediapipe::OkStatus();
    } else {
      return ::mediapipe::NotFoundErrorBuilder(MEDIAPIPE_LOC) << error_msg;
    }
  }

  ::mediapipe::Status Close(CalculatorContext* cc) override {
    auto& options =
        cc->Options().GetExtension(PackMediaSequenceCalculatorOptions::ext);
    if (options.reconcile_metadata()) {
      RET_CHECK_OK(mpms::ReconcileMetadata(options.reconcile_bbox_annotations(),
                                           sequence_.get()));
    }

    if (options.output_only_if_all_present()) {
      ::mediapipe::Status status = VerifySequence();
      if (!status.ok()) {
        cc->GetCounter(status.error_message())->Increment();
        return status;
      }
    }

    if (cc->OutputSidePackets().HasTag(kSequenceExampleTag)) {
      cc->OutputSidePackets()
          .Tag(kSequenceExampleTag)
          .Set(MakePacket<tensorflow::SequenceExample>(*sequence_));
    }
    if (cc->Outputs().HasTag(kSequenceExampleTag)) {
      cc->Outputs()
          .Tag(kSequenceExampleTag)
          .Add(sequence_.release(), Timestamp::PostStream());
    }
    sequence_.reset();

    return ::mediapipe::OkStatus();
  }

  ::mediapipe::Status Process(CalculatorContext* cc) override {
    for (const auto& tag : cc->Inputs().GetTags()) {
      if (absl::StartsWith(tag, kImageTag) &&
          !cc->Inputs().Tag(tag).IsEmpty()) {
        std::string key = "";
        if (tag != kImageTag) {
          int tag_length = sizeof(kImageTag) / sizeof(*kImageTag) - 1;
          if (tag[tag_length] == '_') {
            key = tag.substr(tag_length + 1);
          } else {
            continue;  // Skip keys that don't match "(kImageTag)_?"
          }
        }
        const OpenCvImageEncoderCalculatorResults& image =
            cc->Inputs().Tag(tag).Get<OpenCvImageEncoderCalculatorResults>();
        if (!image.has_encoded_image()) {
          return ::mediapipe::InvalidArgumentErrorBuilder(MEDIAPIPE_LOC)
                 << "No encoded image";
        }
        mpms::AddImageTimestamp(key, cc->InputTimestamp().Value(),
                                sequence_.get());
        mpms::AddImageEncoded(key, image.encoded_image(), sequence_.get());
      }
    }
    if (cc->Inputs().HasTag(kForwardFlowEncodedTag) &&
        !cc->Inputs().Tag(kForwardFlowEncodedTag).IsEmpty()) {
      const OpenCvImageEncoderCalculatorResults& forward_flow =
          cc->Inputs()
              .Tag(kForwardFlowEncodedTag)
              .Get<OpenCvImageEncoderCalculatorResults>();
      if (!forward_flow.has_encoded_image()) {
        return ::mediapipe::InvalidArgumentErrorBuilder(MEDIAPIPE_LOC)
               << "No encoded forward flow";
      }
      mpms::AddForwardFlowTimestamp(cc->InputTimestamp().Value(),
                                    sequence_.get());
      mpms::AddForwardFlowEncoded(forward_flow.encoded_image(),
                                  sequence_.get());
    }
    for (const auto& tag : cc->Inputs().GetTags()) {
      if (absl::StartsWith(tag, kFloatFeaturePrefixTag) &&
          !cc->Inputs().Tag(tag).IsEmpty()) {
        std::string key = tag.substr(sizeof(kFloatFeaturePrefixTag) /
                                         sizeof(*kFloatFeaturePrefixTag) -
                                     1);
        mpms::AddFeatureTimestamp(key, cc->InputTimestamp().Value(),
                                  sequence_.get());
        mpms::AddFeatureFloats(key,
                               cc->Inputs().Tag(tag).Get<std::vector<float>>(),
                               sequence_.get());
      }
    }
    for (const auto& tag : cc->Inputs().GetTags()) {
      if (absl::StartsWith(tag, kBBoxTag) && !cc->Inputs().Tag(tag).IsEmpty()) {
        std::string key = "";
        if (tag != kBBoxTag) {
          int tag_length = sizeof(kBBoxTag) / sizeof(*kBBoxTag) - 1;
          if (tag[tag_length] == '_') {
            key = tag.substr(tag_length + 1);
          } else {
            continue;  // Skip keys that don't match "(kBBoxTag)_?"
          }
        }
        std::vector<Location> predicted_locations;
        std::vector<std::string> predicted_class_strings;
        std::vector<int> predicted_label_ids;
        for (auto& detection :
             cc->Inputs().Tag(tag).Get<std::vector<Detection>>()) {
          if (detection.location_data().format() ==
                  LocationData::BOUNDING_BOX ||
              detection.location_data().format() ==
                  LocationData::RELATIVE_BOUNDING_BOX) {
            int height = mpms::GetImageHeight(*sequence_);
            int width = mpms::GetImageWidth(*sequence_);
            Location relative_bbox = Location::CreateRelativeBBoxLocation(
                Location(detection.location_data())
                    .ConvertToRelativeBBox(width, height));
            predicted_locations.push_back(relative_bbox);
            if (detection.label_size() > 0) {
              predicted_class_strings.push_back(detection.label(0));
            }
            if (detection.label_id_size() > 0) {
              predicted_label_ids.push_back(detection.label_id(0));
            }
          }
        }
        if (!predicted_locations.empty()) {
          mpms::AddBBox(key, predicted_locations, sequence_.get());
          mpms::AddBBoxTimestamp(key, cc->InputTimestamp().Value(),
                                 sequence_.get());
          if (!predicted_class_strings.empty()) {
            mpms::AddBBoxClassString(key, predicted_class_strings,
                                     sequence_.get());
          }
          if (!predicted_label_ids.empty()) {
            mpms::AddBBoxClassIndex(key, predicted_label_ids, sequence_.get());
          }
        }
      }
    }
    if (cc->Inputs().HasTag(kSegmentationMaskTag) &&
        !cc->Inputs().Tag(kSegmentationMaskTag).IsEmpty()) {
      bool already_has_mask = false;
      for (auto& detection : cc->Inputs()
                                 .Tag(kSegmentationMaskTag)
                                 .Get<std::vector<Detection>>()) {
        if (detection.location_data().format() == LocationData::MASK) {
          RET_CHECK(!already_has_mask)
              << "We currently only support adding one mask per timestamp. "
              << sequence_->DebugString();
          auto mask_mat_ptr = Location(detection.location_data()).GetCvMask();
          std::vector<uchar> bytes;
          RET_CHECK(cv::imencode(".png", *mask_mat_ptr, bytes, {}));

          std::string encoded_mask(bytes.begin(), bytes.end());
          mpms::AddClassSegmentationEncoded(encoded_mask, sequence_.get());
          mpms::AddClassSegmentationTimestamp(cc->InputTimestamp().Value(),
                                              sequence_.get());
          // SegmentationClassLabelString is a context feature for the entire
          // sequence. The values in the last detection will be saved.
          mpms::SetClassSegmentationClassLabelString({detection.label(0)},
                                                     sequence_.get());
          already_has_mask = true;
        } else {
          return ::mediapipe::UnimplementedError(
              "Global detections and empty detections are not supported.");
        }
      }
    }
    for (const auto& tag : cc->Inputs().GetTags()) {
      if (!cc->Inputs().Tag(tag).IsEmpty()) {
        features_present_[tag] = true;
      }
    }
    return ::mediapipe::OkStatus();
  }

  std::unique_ptr<tf::SequenceExample> sequence_;
  std::map<std::string, bool> features_present_;
};
REGISTER_CALCULATOR(PackMediaSequenceCalculator);

}  // namespace mediapipe
