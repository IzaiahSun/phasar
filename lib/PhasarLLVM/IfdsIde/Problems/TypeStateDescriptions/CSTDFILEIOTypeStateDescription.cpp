/******************************************************************************
 * Copyright (c) 2018 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#include <cassert>

#include <phasar/PhasarLLVM/IfdsIde/Problems/TypeStateDescriptions/CSTDFILEIOTypeStateDescription.h>

using namespace std;
using namespace psr;

namespace psr {

// Return value is modeled as -1
const std::map<std::string, std::set<int>>
    CSTDFILEIOTypeStateDescription::StdFileIOFuncs = {
        {"fopen", {-1}},   {"fdopen", {-1}},   {"fclose", {0}},
        {"fread", {3}},    {"fwrite", {3}},    {"fgetc", {0}},
        {"fgetwc", {0}},   {"fgets", {2}},     {"getc", {0}},
        {"getwc", {0}},    {"_IO_getc", {0}},  {"ungetc", {1}},
        {"ungetwc", {1}},  {"fputc", {1}},     {"fputwc", {1}},
        {"fputs", {1}},    {"putc", {1}},      {"putwc", {1}},
        {"_IO_putc", {1}}, {"fprintf", {0}},   {"fwprintf", {0}},
        {"vfprintf", {0}}, {"vfwprintf", {0}}, {"__isoc99_fscanf", {0}},{"fscanf", {0}},
        {"fwscanf", {0}},  {"vfscanf", {0}},   {"vfwscanf", {0}},
        {"fflush", {0}},   {"fseek", {0}},     {"ftell", {0}},
        {"rewind", {0}},   {"fgetpos", {0}},   {"fsetpos", {0}},
        {"fileno", {0}}};

// delta[Token][State] = next State
// Token: FOPEN = 0, FCLOSE = 1, STAR = 2
// States: UNINIT = 0, OPENED = 1, CLOSED = 2, ERROR = 3
const CSTDFILEIOTypeStateDescription::CSTDFILEIOState
    CSTDFILEIOTypeStateDescription::delta[3][4] = {
        /* FOPEN */
        {CSTDFILEIOState::OPENED, CSTDFILEIOState::OPENED,
         CSTDFILEIOState::ERROR, CSTDFILEIOState::ERROR},
        /* FCLOSE */
        {CSTDFILEIOState::ERROR, CSTDFILEIOState::CLOSED,
         CSTDFILEIOState::ERROR, CSTDFILEIOState::ERROR},
        /* STAR */
        {CSTDFILEIOState::ERROR, CSTDFILEIOState::OPENED,
         CSTDFILEIOState::ERROR, CSTDFILEIOState::ERROR},
};

bool CSTDFILEIOTypeStateDescription::isFactoryFunction(
    const std::string &F) const {
  if (isAPIFunction(F)) {
    return StdFileIOFuncs.at(F).find(-1) != StdFileIOFuncs.at(F).end();
  }
  return false;
}

bool CSTDFILEIOTypeStateDescription::isConsumingFunction(
    const std::string &F) const {
  if (isAPIFunction(F)) {
    return StdFileIOFuncs.at(F).find(-1) == StdFileIOFuncs.at(F).end();
  }
  return false;
}

bool CSTDFILEIOTypeStateDescription::isAPIFunction(const std::string &F) const {
  return StdFileIOFuncs.find(F) != StdFileIOFuncs.end();
}

TypeStateDescription::State
CSTDFILEIOTypeStateDescription::getNextState(std::string Tok,
                                             TypeStateDescription::State S) const {
  if (isAPIFunction(Tok)) {
    return delta[static_cast<std::underlying_type_t<CSTDFILEIOToken>>(
        funcNameToToken(Tok))][S];
  } else {
    return CSTDFILEIOState::BOT;
  }
}

std::string CSTDFILEIOTypeStateDescription::getTypeNameOfInterest() const {
  return "struct._IO_FILE";
}

set<int> CSTDFILEIOTypeStateDescription::getConsumerParamIdx(
    const std::string &F) const {
  if (isConsumingFunction(F)) {
    return StdFileIOFuncs.at(F);
  }
  return {};
}

set<int>
CSTDFILEIOTypeStateDescription::getFactoryParamIdx(const std::string &F) const {
  if (isFactoryFunction(F)) {
    // Trivial here, since we only generate via return value
    return {-1};
  }
  return {};
}

std::string CSTDFILEIOTypeStateDescription::stateToString(
    TypeStateDescription::State S) const {
  switch (S) {
  case CSTDFILEIOState::TOP:
    return "TOP";
    break;
  case CSTDFILEIOState::UNINIT:
    return "UNINIT";
    break;
  case CSTDFILEIOState::OPENED:
    return "OPENED";
    break;
  case CSTDFILEIOState::CLOSED:
    return "CLOSED";
    break;
  case CSTDFILEIOState::ERROR:
    return "ERROR";
    break;
  case CSTDFILEIOState::BOT:
    return "BOT";
    break;
  default:
    assert(false && "received unknown state!");
    break;
  }
}

TypeStateDescription::State CSTDFILEIOTypeStateDescription::bottom() const {
  return CSTDFILEIOState::BOT;
}

TypeStateDescription::State CSTDFILEIOTypeStateDescription::top() const {
  return CSTDFILEIOState::TOP;
}

TypeStateDescription::State CSTDFILEIOTypeStateDescription::uninit() const {
  return CSTDFILEIOState::UNINIT;
}

TypeStateDescription::State CSTDFILEIOTypeStateDescription::start() const {
  return CSTDFILEIOState::OPENED;
}

TypeStateDescription::State CSTDFILEIOTypeStateDescription::error() const {
  return CSTDFILEIOState::ERROR;
}

CSTDFILEIOTypeStateDescription::CSTDFILEIOToken
CSTDFILEIOTypeStateDescription::funcNameToToken(const std::string &F) const {
  if (F == "fopen" || F == "fdopen")
    return CSTDFILEIOToken::FOPEN;
  else if (F == "fclose")
    return CSTDFILEIOToken::FCLOSE;
  else
    return CSTDFILEIOToken::STAR;
}

} // namespace psr
