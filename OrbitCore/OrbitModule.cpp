// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitModule.h"

#include <cinttypes>
#include <string>

#include "Core.h"
#include "ElfUtils/ElfFile.h"
#include "OrbitBase/Logging.h"
#include "Pdb.h"
#include "ScopeTimer.h"
#include "absl/strings/str_format.h"
#include "symbol.pb.h"

#ifndef WIN32
#include "Capture.h"
#include "LinuxUtils.h"
#include "OrbitProcess.h"
#include "OrbitUnreal.h"
#include "Params.h"
#include "Path.h"
#include "ScopeTimer.h"
#endif

//-----------------------------------------------------------------------------
Module::Module(const std::string& file_name, uint64_t address_start,
               uint64_t address_end) {
  if (!Path::FileExists(file_name)) {
    ERROR("Creating Module from path \"%s\": file does not exist",
          file_name.c_str());
  }

  m_FullName = file_name;
  m_Name = Path::GetFileName(file_name);
  m_Directory = Path::GetDirectory(file_name);
  m_PdbSize = Path::FileSize(file_name);

  m_AddressStart = address_start;
  m_AddressEnd = address_end;

  m_PrettyName = m_FullName;
  m_AddressRange = absl::StrFormat("[%016" PRIx64 " - %016" PRIx64 "]",
                                   m_AddressStart, m_AddressEnd);

  loadable_ = true;  // necessary, because it toggles "Load Symbols" option
}

//-----------------------------------------------------------------------------
std::string Module::GetPrettyName() {
  if (m_PrettyName.empty()) {
#ifdef WIN32
    m_PrettyName =
        absl::StrFormat("%s [%I64x - %I64x] %s\r\n", m_Name.c_str(),
                        m_AddressStart, m_AddressEnd, m_FullName.c_str());
    m_AddressRange =
        absl::StrFormat("[%I64x - %I64x]", m_AddressStart, m_AddressEnd);
#else
    m_PrettyName = m_FullName;
    m_AddressRange =
        absl::StrFormat("[%016llx - %016llx]", m_AddressStart, m_AddressEnd);
#endif
  }

  return m_PrettyName;
}

//-----------------------------------------------------------------------------
bool Module::LoadDebugInfo() {
  CHECK(m_Pdb != nullptr);
  m_Pdb->SetMainModule(m_AddressStart);

  PRINT_VAR(loadable_);
  if (!loadable_) return false;

  loaded_ = m_Pdb->LoadDataFromPdb();
  return loaded_;
}

//-----------------------------------------------------------------------------
void Module::LoadSymbols(const ModuleSymbols& module_symbols) {
  if (m_Pdb != nullptr) {
    LOG("Warning: Module %s already contained symbols, will override now.",
        m_Name);
  }

  m_Pdb = std::make_shared<Pdb>(m_AddressStart, module_symbols.load_bias(),
                                module_symbols.symbols_file_path(), m_FullName);

  for (const SymbolInfo& symbol_info : module_symbols.symbol_infos()) {
    std::shared_ptr<Function> function = std::make_shared<Function>(
        symbol_info.name(), symbol_info.demangled_name(), symbol_info.address(),
        module_symbols.load_bias(), symbol_info.size(),
        symbol_info.source_file(), symbol_info.source_line());
    m_Pdb->AddFunction(function);
  }

  m_Pdb->ProcessData();
  m_PdbName = module_symbols.symbols_file_path();
  SetLoaded(true);
}

//-----------------------------------------------------------------------------
uint64_t Module::ValidateAddress(uint64_t a_Address) {
  if (ContainsAddress(a_Address)) return a_Address;

  // Treat input address as RVA
  uint64_t newAddress = m_AddressStart + a_Address;
  if (ContainsAddress(newAddress)) return newAddress;

  return 0xbadadd;
}

//-----------------------------------------------------------------------------
void Module::SetLoaded(bool value) { loaded_ = value; }

#ifndef WIN32

//-----------------------------------------------------------------------------
Function* Pdb::FunctionFromName(const std::string& a_Name) {
  uint64_t hash = StringHash(a_Name);
  auto iter = m_StringFunctionMap.find(hash);
  return (iter == m_StringFunctionMap.end()) ? nullptr : iter->second;
}

//-----------------------------------------------------------------------------
Pdb::Pdb(uint64_t module_address, uint64_t load_bias, std::string file_name,
         std::string module_file_name)
    : m_MainModule(module_address),
      load_bias_(load_bias),
      m_FileName(std::move(file_name)),
      m_LoadedModuleName(std::move(module_file_name)) {
  m_Name = Path::GetFileName(m_FileName);
}

//-----------------------------------------------------------------------------
void Pdb::AddFunction(const std::shared_ptr<Function>& function) {
  functions_.push_back(function);
  functions_.back()->SetModulePathAndAddress(GetLoadedModuleName(),
                                             GetHModule());
  functions_.back()->SetOrbitTypeFromName();
}

//-----------------------------------------------------------------------------
void Pdb::ProcessData() {
  std::shared_ptr<Process> process = Capture::GTargetProcess;
  if (process == nullptr) return;

  SCOPE_TIMER_LOG("ProcessData");
  ScopeLock lock(process->GetDataMutex());

  auto& globals = process->GetGlobals();

  for (auto& func : functions_) {
    func->SetModulePathAndAddress(GetLoadedModuleName(), GetHModule());
    process->AddFunction(func);
    GOrbitUnreal.OnFunctionAdded(func.get());
  }

  SCOPE_TIMER_LOG("Find File and Line info");
  for (auto& func : functions_) {
    func->FindFile();
  }

  for (Type& type : m_Types) {
    type.m_Pdb = this;
    Capture::GTargetProcess->AddType(type);
    GOrbitUnreal.OnTypeAdded(&type);
  }

  for (Variable& var : m_Globals) {
    var.m_Pdb = this;
    globals.push_back(&var);
  }

  for (auto& it : m_TypeMap) {
    it.second.m_Pdb = this;
  }

  PopulateFunctionMap();
  PopulateStringFunctionMap();
}

//-----------------------------------------------------------------------------
void Pdb::PopulateFunctionMap() {
  SCOPE_TIMER_LOG("Pdb::PopulateFunctionMap");
  for (auto& function : functions_) {
    m_FunctionMap.insert(std::make_pair(function->Address(), function.get()));
  }
}

//-----------------------------------------------------------------------------
void Pdb::PopulateStringFunctionMap() {
  {
    // SCOPE_TIMER_LOG("Reserving map");
    m_StringFunctionMap.reserve(static_cast<size_t>(1.5f * functions_.size()));
  }

  {
    // SCOPE_TIMER_LOG("Map inserts");
    for (auto& function : functions_) {
      m_StringFunctionMap[function->Hash()] = function.get();
    }
  }
}

//-----------------------------------------------------------------------------
Function* Pdb::GetFunctionFromExactAddress(uint64_t a_Address) {
  uint64_t function_address = a_Address - GetHModule() + load_bias_;
  auto it = m_FunctionMap.find(function_address);
  return (it != m_FunctionMap.end()) ? it->second : nullptr;
}

//-----------------------------------------------------------------------------
Function* Pdb::GetFunctionFromProgramCounter(uint64_t a_Address) {
  if (m_FunctionMap.empty()) {
    return nullptr;
  }

  uint64_t relative_address = a_Address - GetHModule() + load_bias_;
  auto it = m_FunctionMap.upper_bound(relative_address);

  if (it == m_FunctionMap.begin()) {
    return nullptr;
  }

  --it;
  return it->second;
}

#endif

//-----------------------------------------------------------------------------
void Pdb::ApplyPreset(const Preset& preset) {
  SCOPE_TIMER_LOG(absl::StrFormat("Pdb::ApplyPreset - %s", m_Name.c_str()));

  std::string module_name = m_LoadedModuleName;
  auto it = preset.m_Modules.find(module_name);
  if (it != preset.m_Modules.end()) {
    const PresetModule& preset_module = it->second;

    for (uint64_t hash : preset_module.m_FunctionHashes) {
      auto fit = m_StringFunctionMap.find(hash);
      if (fit != m_StringFunctionMap.end()) {
        Function* function = fit->second;
        function->Select();
      }
    }
  }
}
