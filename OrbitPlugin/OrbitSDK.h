// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "OrbitData.h"
#include "OrbitUserData.h"

struct ImGuiContext;

namespace Orbit {

//-----------------------------------------------------------------------------
class Plugin {
 public:
  Plugin() {}
  virtual ~Plugin() {}

  virtual void Create() {}
  virtual void Update() = 0;
  virtual const char* GetName() = 0;
  void SetPluginID(int a_ID) { m_ID = a_ID; }

  // Render Thread
  virtual void Draw(ImGuiContext* a_ImguiContext, int a_Width,
                    int a_Height) = 0;

  // Data Thread
  virtual void ReceiveUserData(const Orbit::UserData* a_Data) = 0;
  virtual void ReceiveOrbitData(const Orbit::Data* a_Data) = 0;

 protected:
  int m_ID;
};

}  // namespace Orbit
