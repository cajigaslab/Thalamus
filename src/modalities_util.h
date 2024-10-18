#pragma once

#include <modalities.h>
#include <base_node.hpp>
#include <xsens_node.hpp>
#include <image_node.h>
#include <text_node.h>

namespace thalamus {
  template <typename T>
  constexpr size_t infer_modalities() {
    size_t result = 0;
    result |= std::is_base_of<AnalogNode, T>::value ? THALAMUS_MODALITY_ANALOG : 0;
    result |= std::is_base_of<MotionCaptureNode, T>::value ? THALAMUS_MODALITY_MOCAP : 0;
    result |= std::is_base_of<ImageNode, T>::value ? THALAMUS_MODALITY_IMAGE : 0;
    result |= std::is_base_of<TextNode, T>::value ? THALAMUS_MODALITY_TEXT : 0;
    return result;
  }

  template <typename T>
  T node_cast(Node* node) {
    if(node == nullptr) {
      return nullptr;
    }
    auto modalities = node->modalities();
    if constexpr (std::is_same<T, AnalogNode*>::value) {
      return (modalities & THALAMUS_MODALITY_ANALOG) ? dynamic_cast<T>(node) : nullptr;
    } else if constexpr (std::is_same<T, MotionCaptureNode*>::value) {
      return (modalities & THALAMUS_MODALITY_MOCAP) ? dynamic_cast<T>(node) : nullptr;
    } else if constexpr (std::is_same<T, ImageNode*>::value) {
      return (modalities & THALAMUS_MODALITY_IMAGE) ? dynamic_cast<T>(node) : nullptr;
    } else if constexpr (std::is_same<T, TextNode*>::value) {
      return (modalities & THALAMUS_MODALITY_TEXT) ? dynamic_cast<T>(node) : nullptr;
    } else {
      return dynamic_cast<T>(node);
    }
  }
}
