/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

// do not include any headers here, they must all be in QRDInterface.h
#include "QRDInterface.h"
#include "RemoteHost.h"

class QMutex;

DOCUMENT(R"(Contains the output from invoking a :class:`ShaderProcessingTool`, including both the
actual output data desired as well as any stdout/stderr messages.
)");
struct ShaderToolOutput
{
  DOCUMENT(R"(The output log - containing the information about the tool run and any errors.

:type: str
)");
  rdcstr log;

  DOCUMENT(R"(The actual output data from the tool

:type: bytes
)");
  bytebuf result;
};

DOCUMENT(R"(Describes an external program that can be used to process shaders, typically either
compiling from a high-level language to a binary format, or decompiling from the binary format to
a high-level language or textual representation.

Commonly used with SPIR-V.
)");
struct ShaderProcessingTool
{
  DOCUMENT("");
  ShaderProcessingTool() = default;
  VARIANT_CAST(ShaderProcessingTool);
  bool operator==(const ShaderProcessingTool &o) const
  {
    return tool == o.tool && name == o.name && executable == o.executable && args == o.args &&
           input == o.input && output == o.output;
  }
  bool operator<(const ShaderProcessingTool &o) const
  {
    if(tool != o.tool)
      return tool < o.tool;
    if(name != o.name)
      return name < o.name;
    if(executable != o.executable)
      return executable < o.executable;
    if(args != o.args)
      return args < o.args;
    if(input != o.input)
      return input < o.input;
    if(output != o.output)
      return output < o.output;
    return false;
  }
  DOCUMENT(R"(The :class:`KnownShaderTool` identifying which known tool this program is.

:type: renderdoc.KnownShaderTool
)");
  KnownShaderTool tool = KnownShaderTool::Unknown;
  DOCUMENT(R"(The human-readable name of the program.

:type: str
)");
  rdcstr name;
  DOCUMENT(R"(The path to the executable to run for this program.

:type: str
)");
  rdcstr executable;
  DOCUMENT(R"(The command line argmuents to pass to the program.

:type: str
)");
  rdcstr args;
  DOCUMENT(R"(The input that this program expects.

:type: renderdoc.ShaderEncoding
)");
  ShaderEncoding input = ShaderEncoding::Unknown;
  DOCUMENT(R"(The output that this program provides.

:type: renderdoc.ShaderEncoding
)");
  ShaderEncoding output = ShaderEncoding::Unknown;

  DOCUMENT(R"(Return the default arguments used when invoking this tool

:return: The arguments specified for this tool.
:rtype: str
)");
  rdcstr DefaultArguments() const;

  DOCUMENT(R"(Runs this program to disassemble a given shader reflection.

:param QWidget window: A handle to the window to use when showing a progress bar or error messages.
:param renderdoc.ShaderReflection shader: The shader to disassemble.
:param str args: arguments to pass to the tool. The default arguments can be obtained using
  :meth:`DefaultArguments` which can then be customised as desired. Passing an empty string uses the
  default arguments.
:return: The result of running the tool.
:rtype: ShaderToolOutput
)");
  ShaderToolOutput DisassembleShader(QWidget *window, const ShaderReflection *shader,
                                     rdcstr args) const;

  DOCUMENT(R"(Runs this program to disassemble a given shader source.

:param QWidget window: A handle to the window to use when showing a progress bar or error messages.
:param str source: The source code, preprocessed into a single file.
:param str entryPoint: The name of the entry point in the shader to compile.
:param renderdoc.ShaderStage stage: The pipeline stage that this shader represents.
:param str spirvVer: The version of SPIR-V in use for this shader, or an empty string for defaults.
  The current version can be obtained from reflection data via the ``@spirver`` compile flag.
:param str args: arguments to pass to the tool. The default arguments can be obtained using
  :meth:`DefaultArguments` which can then be customised as desired. Passing an empty string uses the
  default arguments.
:return: The result of running the tool.
:rtype: ShaderToolOutput
)");
  ShaderToolOutput CompileShader(QWidget *window, rdcstr source, rdcstr entryPoint,
                                 ShaderStage stage, rdcstr spirvVer, rdcstr args) const;

private:
  DOCUMENT("Internal function");
  rdcstr IOArguments() const;
};

DECLARE_REFLECTION_STRUCT(ShaderProcessingTool);

#if !defined(SWIG)
#define BUGREPORT_URL "https://renderdoc.org/bugreporter"
#endif

DOCUMENT("Describes a submitted bug report.");
struct BugReport
{
  DOCUMENT("");
  BugReport() { unreadUpdates = false; }
  VARIANT_CAST(BugReport);
  bool operator==(const BugReport &o) const
  {
    return reportId == o.reportId && submitDate == o.submitDate && checkDate == o.checkDate &&
           unreadUpdates == o.unreadUpdates;
  }
  bool operator<(const BugReport &o) const
  {
    if(reportId != o.reportId)
      return reportId < o.reportId;
    if(submitDate != o.submitDate)
      return submitDate < o.submitDate;
    if(checkDate != o.checkDate)
      return checkDate < o.checkDate;
    if(unreadUpdates != o.unreadUpdates)
      return unreadUpdates < o.unreadUpdates;
    return false;
  }
  DOCUMENT(R"(The private ID of the bug report.

:type: str
)");
  rdcstr reportId;
  DOCUMENT(R"(The original date when this bug was submitted.

:type: datetime
)");
  rdcdatetime submitDate;
  DOCUMENT(R"(The last date that we checked for updates.

:type: datetime
)");
  rdcdatetime checkDate;
  DOCUMENT(R"(Unread updates to the bug exist

:type: bool
)");
  bool unreadUpdates = false;

  DOCUMENT(R"(Gets the URL for this report.

:return: The URL to the report.
:rtype: str
)");
  rdcstr URL() const;
};

DECLARE_REFLECTION_STRUCT(BugReport);

#define CONFIG_SETTING_VAL(access, variantType, type, name, defaultValue) \
  access:                                                                 \
  type name = defaultValue;
#define CONFIG_SETTING(access, variantType, type, name) \
  access:                                               \
  type name;

// Since this macro is already complex enough, the documentation for each of these members is
// in the docstring for PersistantConfig as :data: members.
// Please keep that docstring up to date when you add/remove/change these config settings.
// Note that only public properties should be documented.
#define CONFIG_SETTINGS()                                                                          \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The style to load for the UI. Possible values include 'Native', 'RDLight', 'RDDark'. "      \
      "If empty, the closest of RDLight and RDDark will be chosen, based on the overall "          \
      "light-on-dark or dark-on-light theme of the application native style."                      \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, UIStyle, "")                                         \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The path to the last capture to be opened, which is useful as a default location for "      \
      "browsing."                                                                                  \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, LastCaptureFilePath, "")                             \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The path to the last file browsed to in any dialog. Used as a default location for all "    \
      "file browsers without another explicit default directory (such as opening capture files - " \
      "see :data:`LastCaptureFilePath`)."                                                          \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, LastFileBrowsePath, "")                              \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The recently opened capture files.\n"                                                       \
      "\n:"                                                                                        \
      "type: List[str]");                                                                          \
  CONFIG_SETTING(public, QVariantList, rdcarray<rdcstr>, RecentCaptureFiles)                       \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The path containing the last executable that was captured, which is useful as a default "   \
      "location for browsing."                                                                     \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, LastCapturePath, "")                                 \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The filename of the last executable that was captured, inside :data:`LastCapturePath`."     \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, LastCaptureExe, "")                                  \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The recently opened capture settings files.\n"                                              \
      "\n:"                                                                                        \
      "type: List[str]");                                                                          \
  CONFIG_SETTING(public, QVariantList, rdcarray<rdcstr>, RecentCaptureSettings)                    \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The path to where temporary capture files should be stored until they're saved "            \
      "permanently."                                                                               \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, TemporaryCaptureDirectory, "")                       \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "The default path to save captures in, when browsing to save a temporary capture "           \
      "somewhere."                                                                                 \
      ""                                                                                           \
      ":type: str");                                                                               \
  CONFIG_SETTING_VAL(public, QString, rdcstr, DefaultCaptureSaveDirectory, "")                     \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "A :class:`ReplayOptions` containing the configured default replay options to use in most "  \
      "scenarios when no specific options are given.\n"                                            \
      "\n:"                                                                                        \
      "type: renderdoc.ReplayOptions");                                                            \
  CONFIG_SETTING(public, QVariant, ReplayOptions, DefaultReplayOptions)                            \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "``True`` if the :class:`TextureViewer` should reset the visible range when a new texture "  \
      "is selected.\n"                                                                             \
      "\n:"                                                                                        \
      "Defaults to ``False``."                                                                     \
      ""                                                                                           \
      ":type: bool");                                                                              \
  CONFIG_SETTING_VAL(public, bool, bool, TextureViewer_ResetRange, false)                          \
                                                                                                   \
  DOCUMENT(                                                                                        \
      "``True`` if the :class:`TextureViewer` should store most visualisation settings on a "      \
      "per-texture basis instead of keeping it persistent across different textures.\n"            \
      "\n:"                                                