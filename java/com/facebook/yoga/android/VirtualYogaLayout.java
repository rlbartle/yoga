/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.facebook.yoga.android;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.Iterator;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewParent;
import android.view.ViewGroup;

import com.facebook.yoga.YogaConstants;
import com.facebook.yoga.YogaDisplay;
import com.facebook.yoga.YogaNode;
import com.facebook.yoga.YogaNodeFactory;

/**
 * Much like a {@link YogaLayout}, except this class does not render itself (the container) to the
 * screen.  As a result, <i>do not use this if you wish the container to have a background or
 * foreground</i>.  However, all of its children will still render as expected.
 *
 * <p>
 * In practice, this class never added to the View tree, and all its children become children of its
 * parent.  As a result, all the layout (such as the traversal of the tree) is performed by Yoga
 * (and so natively) increasing performance.
 */
public class VirtualYogaLayout extends ViewGroup {

  final private Set<YogaLayout.ViewNodePair> mYogaNodes = new LinkedHashSet<>();
  final private YogaNode mYogaNode = YogaNodeFactory.create();

  public VirtualYogaLayout(Context context) {
    super(context);
  }

  public VirtualYogaLayout(Context context, AttributeSet attrs) {
    this(context, attrs, 0);
  }

  public VirtualYogaLayout(Context context, AttributeSet attrs, int defStyleAttr) {
    super(context, attrs, defStyleAttr);
    YogaLayout.LayoutParams lp = new YogaLayout.LayoutParams(context, attrs);
    YogaLayout.applyLayoutParams(lp, mYogaNode, this);
  }

  public YogaNode getYogaNode() {
    return mYogaNode;
  }

  /**
   * Called to add a view, creating a new yoga node for it and adding that yoga node to the parent.
   * If the child is a {@link VirtualYogaLayout}, we simply transfer all its children to this one
   * in a manner that maintains the tree, and add its root to the tree.
   *
   * @param child  the View to add
   * @param index  the position at which to add it (ignored)
   * @param params the layout parameters to apply
   */
  @Override
  public void addView(View child, int index, ViewGroup.LayoutParams params) {
    if (child instanceof VirtualYogaLayout) {
      ((VirtualYogaLayout) child).transferChildren(this);

      final YogaNode childNode = ((VirtualYogaLayout) child).getYogaNode();
      mYogaNode.addChildAt(childNode, mYogaNode.getChildCount());

      return;
    }

    YogaNode node = YogaNodeFactory.create();
    YogaLayout.LayoutParams lp = new YogaLayout.LayoutParams(params);
    YogaLayout.applyLayoutParams(lp, node, child);
    node.setData(child);
    node.setMeasureFunction(new YogaLayout.ViewMeasureFunction());

    mYogaNode.addChildAt(node, mYogaNode.getChildCount());

    addView(child, node);
  }

  /**
   * Called to add a view with a corresponding node.
   *
   * @param child the View to add
   * @param node  the corresponding yoga node
   */
  public void addView(View child, YogaNode node) {
    mYogaNodes.add(new YogaLayout.ViewNodePair(child, node));
    if (node.getOwner() == null)
      mYogaNode.addChildAt(node, mYogaNode.getChildCount());
  }

  void addViewNodePair(YogaLayout.ViewNodePair pair) {
    mYogaNodes.add(pair);
    if (pair.node.getOwner() == null)
      mYogaNode.addChildAt(pair.node, mYogaNode.getChildCount());
  }

  @Override
  public void removeView(View view) {
    removeViewFromYogaTree(view);
    super.removeView(view);
  }

  @Override
  public void removeViewAt(int index) {
    removeViewFromYogaTree(getChildAt(index));
    super.removeViewAt(index);
  }

  @Override
  public void removeViewInLayout(View view) {
    removeViewFromYogaTree(view);
    super.removeViewInLayout(view);
  }

  @Override
  public void removeViews(int start, int count) {
    for (int i = start; i < start + count; i++) {
      removeViewFromYogaTree(getChildAt(i));
    }
    super.removeViews(start, count);
  }

  @Override
  public void removeViewsInLayout(int start, int count) {
    for (int i = start; i < start + count; i++) {
      removeViewFromYogaTree(getChildAt(i));
    }
    super.removeViewsInLayout(start, count);
  }

  @Override
  public void removeAllViews() {
    for (Iterator<YogaLayout.ViewNodePair> it = mYogaNodes.iterator(); it.hasNext(); ) {
      removeFromYogaTree(it);
    }
    super.removeAllViews();
  }

  @Override
  public void removeAllViewsInLayout() {
    for (Iterator<YogaLayout.ViewNodePair> it = mYogaNodes.iterator(); it.hasNext(); ) {
      removeFromYogaTree(it);
    }
    super.removeAllViewsInLayout();
  }

  public void removeFromParentView() {
    //Virtual layouts are not actually added so don't have a parent view.
    //However the children managed by the virtual layout should be removed
    //from their parent which will produce the expected behaviour as if
    //this layout were connected.
    for (YogaLayout.ViewNodePair pair : mYogaNodes) {
      ViewParent parent = pair.view.getParent();
      if (parent instanceof ViewGroup) {
        ((ViewGroup) parent).removeView(pair.view);
      }
    }

    //Remove this node from the yoga tree.
    YogaNode node = mYogaNode.getOwner();
    if (node != null) {
      int index = node.indexOf(mYogaNode);
      if (index != -1) {
        node.removeChildAt(index);
      }
    }
  }

  private void removeFromYogaTree(Iterator<YogaLayout.ViewNodePair> it) {
    YogaLayout.ViewNodePair pair = it.next();
    final YogaNode node = pair.node;

    //Only remove the node if this is the owner.  (VirtualLayouts own nodes and we
    //only want to remove the virtuallayout, not have the children removed from the
    //virtuallayout)
    if (node.getOwner() == mYogaNode) {
      int index = mYogaNode.indexOf(node);
      if (index != -1) {
        mYogaNode.removeChildAt(index);
      }
    }

    it.remove();
  }

  private void removeViewFromYogaTree(View view) {
    for (Iterator<YogaLayout.ViewNodePair> it = mYogaNodes.iterator(); it.hasNext(); ) {
      YogaLayout.ViewNodePair pair = it.next();
      if (pair.view == view) {

        //Only remove the node if this is the owner.  (VirtualLayouts own nodes and we
        //only want to remove the virtuallayout, not have the children removed from the
        //virtuallayout)
        if (pair.node.getOwner() == mYogaNode) {
          int index = mYogaNode.indexOf(pair.node);
          if (index != -1)
            mYogaNode.removeChildAt(index);
        }

        it.remove();
      }
    }
  }

  @Override
  public void setVisibility(int visibility) {
    super.setVisibility(visibility);

    if (getVisibility() != GONE) {
      //Is part of the layout
      mYogaNode.setDisplay(YogaDisplay.FLEX);
    } else {
      //Removed from the layout
      mYogaNode.setDisplay(YogaDisplay.NONE);
    }
  }

  /**
   * Gives up children {@code View}s to the parent, maintaining the Yoga tree.  This function calls
   * {@link YogaLayout#addView(View, YogaNode)} or {@link VirtualYogaLayout#addView(View, YogaNode)}
   * on the parent to add the {@code View} without generating new yoga nodes.
   *
   * @param parent the parent to pass children to (must be a YogaLayout or a VirtualYogaLayout)
   */
  protected void transferChildren(ViewGroup parent) {
    if (parent instanceof VirtualYogaLayout) {
      for (YogaLayout.ViewNodePair pair : mYogaNodes) {
        ((VirtualYogaLayout) parent).addViewNodePair(pair);
      }
    } else if (parent instanceof YogaLayout) {
      for (YogaLayout.ViewNodePair pair : mYogaNodes) {
        ((YogaLayout) parent).addViewNodePair(pair);
      }
    } else {
      throw new RuntimeException("VirtualYogaLayout cannot transfer children to ViewGroup of type "
        + parent.getClass().getCanonicalName() + ".  Must either be a VirtualYogaLayout or a " +
        "YogaLayout.");
    }
  }

  @Override
  protected void onLayout(boolean changed, int l, int t, int r, int b) {
    throw new RuntimeException("Attempting to layout a VirtualYogaLayout");
  }

  @Override
  public ViewGroup.LayoutParams generateLayoutParams(AttributeSet attrs) {
    return new YogaLayout.LayoutParams(getContext(), attrs);
  }

  @Override
  protected ViewGroup.LayoutParams generateDefaultLayoutParams() {
    return new YogaLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      ViewGroup.LayoutParams.MATCH_PARENT);
  }

  @Override
  protected ViewGroup.LayoutParams generateLayoutParams(ViewGroup.LayoutParams p) {
    return new YogaLayout.LayoutParams(p);
  }

  @Override
  protected boolean checkLayoutParams(ViewGroup.LayoutParams p) {
    return p instanceof YogaLayout.LayoutParams;
  }
}
