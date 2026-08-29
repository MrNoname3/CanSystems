#pragma once
#include <stdint.h>                                                 /// Standard fixed-width integer types.

/// @brief Append-only intrusive list of registered handlers.
/// @details The link lives in the node itself, so registering one costs no allocation - which is
/// what an embedded target wants. Both users register from their own constructor and never
/// remove anything, so the list only ever grows, and appending is O(1) through the kept tail.
///
/// Kept free of the CAN handler, the MQTT client and the FreeRTOS plumbing so it can be
/// unit-tested on the host, the same way CanFramePump and OtaCanResponse are.
///
/// @tparam Node Node type providing `getNext()` and `setNext()`.
template<typename Node>
class IntrusiveList final {
public:
  /// @brief Registers a node at the end of the list.
  /// @param node Node to register.
  /// @return `false` when `node` is null; `true` once it is linked in.
  [[nodiscard]] bool append(Node* node) {
    if(node == nullptr) { return false; }
    if(tail != nullptr) {
      tail->setNext(node);
    } else {
      head = node;
    }
    tail = node;
    return true;
  }

  /// @brief Finds the first registered node a predicate accepts.
  /// @tparam Match Callable taking a `Node*` and returning `true` for the node it wants.
  /// @param match Predicate deciding whether a node is the one being looked for.
  /// @return The first node `match` accepts, or `nullptr` when none does.
  template<typename Match>
  [[nodiscard]] Node* findIf(Match match) const {
    for(Node* node = head; node != nullptr; node = node->getNext()) {
      if(match(node)) { return node; }
    }
    return nullptr;
  }

  /// @brief First registered node, for walking the whole list.
  /// @return The head of the list, or `nullptr` when nothing is registered.
  [[nodiscard]] Node* first() const { return head; }

private:
  Node* head = nullptr;                                             // First registered node.
  Node* tail = nullptr;                                             // Last registered node, kept for O(1) append.
};
