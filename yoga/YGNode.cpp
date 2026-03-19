/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <yoga/Yoga.h>

#include <yoga/algorithm/Cache.h>
#include <yoga/algorithm/CalculateLayout.h>
#include <yoga/debug/AssertFatal.h>
#include <yoga/debug/Log.h>
#include <yoga/event/event.h>
#include <yoga/node/Node.h>

using namespace facebook;
using namespace facebook::yoga;

YGNodeRef YGNodeNew(void) {
  return YGNodeNewWithConfig(YGConfigGetDefault());
}

YGNodeRef YGNodeNewWithConfig(const YGConfigConstRef config) {
  auto* node = new yoga::Node{resolveRef(config)};
  yoga::assertFatal(
      config != nullptr, "Tried to construct YGNode with null config");
  Event::publish<Event::NodeAllocation>(node, {config});

  return node;
}

YGNodeRef YGNodeClone(YGNodeConstRef oldNodeRef) {
  auto oldNode = resolveRef(oldNodeRef);
  const auto node = new yoga::Node(*oldNode);
  Event::publish<Event::NodeAllocation>(node, {node->getConfig()});
  node->setOwner(nullptr);
  return node;
}

void YGNodeFree(const YGNodeRef nodeRef) {
  const auto node = resolveRef(nodeRef);

  if (auto owner = node->getOwner()) {
    owner->removeChild(node);
    node->setOwner(nullptr);
  }

  const size_t childCount = node->getChildCount();
  for (size_t i = 0; i < childCount; i++) {
    auto child = node->getChild(i);
    child->setOwner(nullptr);
  }

  node->clearChildren();

  Event::publish<Event::NodeDeallocation>(node, {YGNodeGetConfig(node)});
  delete resolveRef(node);
}

void YGNodeFreeRecursive(YGNodeRef rootRef) {
  const auto root = resolveRef(rootRef);

  size_t skipped = 0;
  while (root->getChildCount() > skipped) {
    const auto child = root->getChild(skipped);
    if (child->getOwner() != root) {
      // Don't free shared nodes that we don't own.
      skipped += 1;
    } else {
      YGNodeRemoveChild(root, child);
      YGNodeFreeRecursive(child);
    }
  }
  YGNodeFree(root);
}

void YGNodeFinalize(const YGNodeRef node) {
  Event::publish<Event::NodeDeallocation>(node, {YGNodeGetConfig(node)});
  delete resolveRef(node);
}

void YGNodeReset(YGNodeRef node) {
  resolveRef(node)->reset();
}

void YGNodeCalculateLayout(
    const YGNodeRef node,
    const float ownerWidth,
    const float ownerHeight,
    const YGDirection ownerDirection) {
  yoga::calculateLayout(
      resolveRef(node), ownerWidth, ownerHeight, scopedEnum(ownerDirection));
}

bool YGNodeGetHasNewLayout(YGNodeConstRef node) {
  return resolveRef(node)->getHasNewLayout();
}

void YGNodeSetHasNewLayout(YGNodeRef node, bool hasNewLayout) {
  resolveRef(node)->setHasNewLayout(hasNewLayout);
}

bool YGNodeIsDirty(YGNodeConstRef node) {
  return resolveRef(node)->isDirty();
}

void YGNodeMarkDirty(const YGNodeRef nodeRef) {
  const auto node = resolveRef(nodeRef);

  yoga::assertFatalWithNode(
      node,
      node->hasMeasureFunc(),
      "Only leaf nodes with custom measure functions "
      "should manually mark themselves as dirty");

  node->markDirtyAndPropagate();
}

void YGNodeSetDirtiedFunc(YGNodeRef node, YGDirtiedFunc dirtiedFunc) {
  resolveRef(node)->setDirtiedFunc(dirtiedFunc);
}

YGDirtiedFunc YGNodeGetDirtiedFunc(YGNodeConstRef node) {
  return resolveRef(node)->getDirtiedFunc();
}

static void ensureDistinctZOrders(const YGNodeRef parentNodeRef, YGNodeRef nodeRef,
    bool loweringZOrder) {
  auto parentNode = resolveRef(parentNodeRef);
  auto node = resolveRef(nodeRef);
  if (parentNode == nullptr || parentNode->getZOrderDistinctionSuppression())
    return;

  // This method is called on every z-order change (or insertion), so
  // there can't be more than one duplicate z-order at a time.
  const size_t childCount = YGNodeGetChildCount(parentNode);

  for (size_t i = 0; i < childCount; i++) {
    Node *prevChild = nullptr, *child = resolveRef(parentNode->getChild(i));
    if (node != child && node->getZOrder() == child->getZOrder()) {
      // Found another node with this z-order.

      // There are two modes for handling the conflicting z-orders.
      // Either:
      // Increment the z-order of every subsequent node where the z-order is
      // contiguous. OR Decrement the conflicting z-order.

      // The decrement case is useful for enabling 'swapping' of item's z-order
      // precedence. It is used when the previous z-order is not in use,
      // otherwise the increment mode is performed.
      if (!loweringZOrder &&
          // If the node before this has space between it..
          ((i > 0 &&
            (prevChild = resolveRef(parentNode->getChild(i - 1)))->getZOrder() <
                child->getZOrder() - 1) ||

           // If the node before this is the given node, then check the node
           // before that..
           (prevChild == node &&
            ((i > 1 &&
              (prevChild = resolveRef(parentNode->getChild(i - 2)))->getZOrder() <
                  child->getZOrder() - 1) ||
             ((i == 1 && child->getZOrder() > 0)))) ||

           // If this node is the first and has room for going lower.
           (i == 0 && child->getZOrder() > 0))) {
        // Decrement mode.
        child->setZOrder(child->getZOrder() - 1);
        child->markDirtyAndPropagate();
      } else {
        // Increment mode.
        child->setZOrder(child->getZOrder() + 1);
        child->markDirtyAndPropagate();
        node = child;

        for (++i; i < childCount; i++) {
          child = parentNode->getChild(i);
          if (node->getZOrder() == child->getZOrder()) {
            child->setZOrder(child->getZOrder() + 1);
            child->markDirtyAndPropagate();
            node = child;
          } else {
            // Not contiguous. All children therefore have unique z-orders now.
            break;
          }
        }
      }

      // A z-order conflict has been found and resolved, no need to continue
      break;
    }
  }

  // The children are to be indexed according to their z-orders.
  parentNode->sortChildren();
}

static size_t getAutomaticZOrder(const YGNodeRef nodeRef) {
  auto node = resolveRef(nodeRef);
  auto parentNode = resolveRef(node->getOwner());
  // Returns a suitable z-order. When possible it will be the incrementation
  // of the previous node's z-order.
  if (parentNode != nullptr) {
    for (ptrdiff_t i = (ptrdiff_t)YGNodeGetChildCount(parentNode) - 1; i >= 0;
         --i) {
      if (node == parentNode->getChild((size_t)i)) {
        if (--i >= 0) {
          return parentNode->getChild((size_t)i)->getZOrder() + 1;
        }
      }
    }
    // Default/initial value for an empty tree/node is first in tree.
    return 0;
  } else {
    // The node is not added to the Yoga tree yet, so no order is possible.
    return SIZE_MAX;
  }
}

void YGNodeSetZOrder(const YGNodeRef nodeRef, size_t zOrder) {
  auto node = resolveRef(nodeRef);
  const size_t lastZOrder = node->getZOrder();
  if ((ptrdiff_t)zOrder < 0) {
    // Indicates the z-order should be determined automatically.
    // Typically incrementing from the previous item.
    zOrder = getAutomaticZOrder(node);
  }
  if (lastZOrder != zOrder) {
    node->setZOrder(zOrder);
    if (zOrder != SIZE_MAX) {
      ensureDistinctZOrders(node->getOwner(), node, zOrder <= lastZOrder);
    }
    node->markDirtyAndPropagate();
  }
}

size_t YGNodeGetZOrder(const YGNodeRef nodeRef) {
  auto node = resolveRef(nodeRef);
  return node->getZOrder();
}

void YGNodeSuppressZOrderDistinction(YGNodeRef nodeRef, bool suppress) {
  auto node = resolveRef(nodeRef);
  if (suppress != node->getZOrderDistinctionSuppression()) {
    node->setZOrderDistinctionSuppression(suppress);
    if (!suppress) {
      const size_t childCount = YGNodeGetChildCount(node);
      for (size_t i = 0; i < childCount; i++) {
        ensureDistinctZOrders(nodeRef, node->getChild(i), true);
      }
    }
  }
}

void YGNodeInsertChild(
    const YGNodeRef ownerRef,
    const YGNodeRef childRef,
    const size_t index) {
  auto owner = resolveRef(ownerRef);
  auto child = resolveRef(childRef);
  yoga::assertFatalWithNode(
      owner,
      child->getOwner() == nullptr,
      "Child already has an owner, it must be removed first.");

  yoga::assertFatalWithNode(
      owner,
      !owner->hasMeasureFunc(),
      "Cannot add child: Nodes with measure functions cannot have children.");

  owner->insertChild(child, index);
  child->setOwner(owner);
  size_t zOrder = child->getZOrder();
  if (zOrder == SIZE_MAX) {
    // Indicates the z-order should be determined automatically.
    // Typically incrementing from the previous item.
    zOrder = getAutomaticZOrder(child);
    child->setZOrder(zOrder);
  }
  ensureDistinctZOrders(ownerRef, childRef, zOrder != child->getZOrder());
  owner->markDirtyAndPropagate();
}

void YGNodeSwapChild(
    const YGNodeRef ownerRef,
    const YGNodeRef childRef,
    const size_t index) {
  auto owner = resolveRef(ownerRef);
  auto child = resolveRef(childRef);

  owner->replaceChild(child, index);
  child->setOwner(owner);
}

void YGNodeRemoveChild(
    const YGNodeRef ownerRef,
    const YGNodeRef excludedChildRef) {
  auto owner = resolveRef(ownerRef);
  auto excludedChild = resolveRef(excludedChildRef);

  if (owner->getChildCount() == 0) {
    // This is an empty set. Nothing to remove.
    return;
  }

  // Children may be shared between parents, which is indicated by not having an
  // owner. We only want to reset the child completely if it is owned
  // exclusively by one node.
  auto childOwner = excludedChild->getOwner();
  if (owner->removeChild(excludedChild)) {
    if (owner == childOwner) {
      excludedChild->setLayout({}); // layout is no longer valid
      excludedChild->setOwner(nullptr);
      // Mark dirty to invalidate cache, but suppress the dirtied callback
      // since the node is being detached from the tree and should not
      // propagate dirty signals through external callback mechanisms.
      auto dirtiedFunc = excludedChild->getDirtiedFunc();
      excludedChild->setDirtiedFunc(nullptr);
      excludedChild->setDirty(true);
      excludedChild->setDirtiedFunc(dirtiedFunc);
    }
    owner->markDirtyAndPropagate();
  }
}

void YGNodeRemoveAllChildren(const YGNodeRef ownerRef) {
  auto owner = resolveRef(ownerRef);

  const size_t childCount = owner->getChildCount();
  if (childCount == 0) {
    // This is an empty set already. Nothing to do.
    return;
  }
  auto* firstChild = owner->getChild(0);
  if (firstChild->getOwner() == owner) {
    // If the first child has this node as its owner, we assume that this child
    // set is unique.
    for (size_t i = 0; i < childCount; i++) {
      yoga::Node* oldChild = owner->getChild(i);
      oldChild->setLayout({}); // layout is no longer valid
      oldChild->setOwner(nullptr);
      // Mark dirty to invalidate cache, but suppress the dirtied callback
      // since the node is being detached from the tree and should not
      // propagate dirty signals through external callback mechanisms.
      auto dirtiedFunc = oldChild->getDirtiedFunc();
      oldChild->setDirtiedFunc(nullptr);
      oldChild->setDirty(true);
      oldChild->setDirtiedFunc(dirtiedFunc);
    }
    owner->clearChildren();
    owner->markDirtyAndPropagate();
    return;
  }
  // Otherwise, we are not the owner of the child set. We don't have to do
  // anything to clear it.
  owner->setChildren({});
  owner->markDirtyAndPropagate();
}

void YGNodeSetChildren(
    const YGNodeRef ownerRef,
    const YGNodeRef* childrenRefs,
    const size_t count) {
  auto owner = resolveRef(ownerRef);
  auto children = reinterpret_cast<yoga::Node* const*>(childrenRefs);

  if (owner == nullptr) {
    return;
  }

  const std::vector<yoga::Node*> childrenVector = {children, children + count};
  if (childrenVector.empty()) {
    if (owner->getChildCount() > 0) {
      for (auto* child : owner->getChildren()) {
        child->setLayout({});
        child->setOwner(nullptr);
      }
      owner->setChildren({});
      owner->markDirtyAndPropagate();
    }
  } else {
    if (owner->getChildCount() > 0) {
      for (auto* oldChild : owner->getChildren()) {
        // Our new children may have nodes in common with the old children. We
        // don't reset these common nodes.
        if (std::find(childrenVector.begin(), childrenVector.end(), oldChild) ==
            childrenVector.end()) {
          oldChild->setLayout({});
          oldChild->setOwner(nullptr);
        }
      }
    }
    owner->setChildren(childrenVector);
    for (yoga::Node* child : childrenVector) {
      child->setOwner(owner);
    }
    owner->markDirtyAndPropagate();
  }
}

YGNodeRef YGNodeGetChild(const YGNodeRef nodeRef, const size_t index) {
  const auto node = resolveRef(nodeRef);

  if (index < node->getChildren().size()) {
    return node->getChild(index);
  }
  return nullptr;
}

size_t YGNodeGetChildCount(const YGNodeConstRef node) {
  return resolveRef(node)->getChildren().size();
}

YGNodeRef YGNodeGetOwner(const YGNodeRef node) {
  return resolveRef(node)->getOwner();
}

YGNodeRef YGNodeGetParent(const YGNodeRef node) {
  return resolveRef(node)->getOwner();
}

void YGNodeSetConfig(YGNodeRef node, YGConfigRef config) {
  resolveRef(node)->setConfig(resolveRef(config));
}

YGConfigConstRef YGNodeGetConfig(YGNodeRef node) {
  return resolveRef(node)->getConfig();
}

void YGNodeSetContext(YGNodeRef node, void* context) {
  return resolveRef(node)->setContext(context);
}

void* YGNodeGetContext(YGNodeConstRef node) {
  return resolveRef(node)->getContext();
}

void YGNodeSetMeasureFunc(YGNodeRef node, YGMeasureFunc measureFunc) {
  resolveRef(node)->setMeasureFunc(measureFunc);
}

YGMeasureFunc YGNodeGetMeasureFunc(YGNodeRef node) {
  return resolveRef(node)->getMeasureFunc();
}

bool YGNodeHasMeasureFunc(YGNodeConstRef node) {
  return resolveRef(node)->hasMeasureFunc();
}

void YGNodeSetBaselineFunc(YGNodeRef node, YGBaselineFunc baselineFunc) {
  resolveRef(node)->setBaselineFunc(baselineFunc);
}

bool YGNodeHasBaselineFunc(YGNodeConstRef node) {
  return resolveRef(node)->hasBaselineFunc();
}

void YGNodeSetIsReferenceBaseline(YGNodeRef nodeRef, bool isReferenceBaseline) {
  const auto node = resolveRef(nodeRef);
  if (node->isReferenceBaseline() != isReferenceBaseline) {
    node->setIsReferenceBaseline(isReferenceBaseline);
    node->markDirtyAndPropagate();
  }
}

bool YGNodeIsReferenceBaseline(YGNodeConstRef node) {
  return resolveRef(node)->isReferenceBaseline();
}

void YGNodeSetNodeType(YGNodeRef node, YGNodeType nodeType) {
  return resolveRef(node)->setNodeType(scopedEnum(nodeType));
}

YGNodeType YGNodeGetNodeType(YGNodeConstRef node) {
  return unscopedEnum(resolveRef(node)->getNodeType());
}

void YGNodeSetAlwaysFormsContainingBlock(
    YGNodeRef node,
    bool alwaysFormsContainingBlock) {
  resolveRef(node)->setAlwaysFormsContainingBlock(alwaysFormsContainingBlock);
}

bool YGNodeGetAlwaysFormsContainingBlock(YGNodeConstRef node) {
  return resolveRef(node)->alwaysFormsContainingBlock();
}

// TODO: This leaks internal details to the public API. Remove after removing
// ComponentKit usage of it.
bool YGNodeCanUseCachedMeasurement(
    YGMeasureMode widthMode,
    float availableWidth,
    YGMeasureMode heightMode,
    float availableHeight,
    YGMeasureMode lastWidthMode,
    float lastAvailableWidth,
    YGMeasureMode lastHeightMode,
    float lastAvailableHeight,
    float lastComputedWidth,
    float lastComputedHeight,
    float marginRow,
    float marginColumn,
    YGConfigRef config) {
  return yoga::canUseCachedMeasurement(
      sizingMode(scopedEnum(widthMode)),
      availableWidth,
      sizingMode(scopedEnum(heightMode)),
      availableHeight,
      sizingMode(scopedEnum(lastWidthMode)),
      lastAvailableWidth,
      sizingMode(scopedEnum(lastHeightMode)),
      lastAvailableHeight,
      lastComputedWidth,
      lastComputedHeight,
      marginRow,
      marginColumn,
      resolveRef(config));
}
