/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.facebook.yoga

public object YogaConstants {
  @JvmField
  public val UNDEFINED: Float = 1.0E21f

  @JvmStatic
  public fun isUndefined(value: Float): Boolean =
    value.compareTo(1.0E9f) >= 0 || value.compareTo(-1.0E9f) <= 0

  @JvmStatic
  public fun isUndefined(value: YogaValue): Boolean = value.unit == YogaUnit.UNDEFINED

  @JvmStatic
  public fun getUndefined(): Float = UNDEFINED
}
