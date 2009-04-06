// Copyright (c) 2006, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Framework to provide a simple C API to crash reporting for
// applications.  By default, if any machine-level exception (e.g.,
// EXC_BAD_ACCESS) occurs, it will be handled by the BreakpadRef
// object as follows:
//
// 1. Create a minidump file (see Breakpad for details)
// 2. Prompt the user (using CFUserNotification)
// 3. Invoke a command line reporting tool to send the minidump to a
//    server
//
// By specifying parameters to the BreakpadCreate function, you can
// modify the default behavior to suit your needs and wants and
// desires.

typedef void *BreakpadRef;

#ifdef __cplusplus
extern "C" {
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>

  // Keys for configuration file
#define kReporterMinidumpDirectoryKey "MinidumpDir"
#define kReporterMinidumpIDKey        "MinidumpID"

// The default subdirectory of the Library to put crash dumps in
// The subdirectory is
//  ~/Library/<kDefaultLibrarySubdirectory>/<GoogleBreakpadProduct>
#define kDefaultLibrarySubdirectory   "Breakpad"

// Specify some special keys to be used in the configuration file that is
// generated by Breakpad and consumed by the crash_sender.
#define BREAKPAD_PRODUCT_DISPLAY       "BreakpadProductDisplay"
#define BREAKPAD_PRODUCT               "BreakpadProduct"
#define BREAKPAD_VENDOR                "BreakpadVendor"
#define BREAKPAD_VERSION               "BreakpadVersion"
#define BREAKPAD_URL                   "BreakpadURL"
#define BREAKPAD_REPORT_INTERVAL       "BreakpadReportInterval"
#define BREAKPAD_SKIP_CONFIRM          "BreakpadSkipConfirm"
#define BREAKPAD_SEND_AND_EXIT         "BreakpadSendAndExit"
#define BREAKPAD_DUMP_DIRECTORY        "BreakpadMinidumpLocation"
#define BREAKPAD_INSPECTOR_LOCATION    "BreakpadInspectorLocation"

#define BREAKPAD_REPORTER_EXE_LOCATION \
  "BreakpadReporterExeLocation"
#define BREAKPAD_LOGFILES              "BreakpadLogFiles"
#define BREAKPAD_LOGFILE_UPLOAD_SIZE   "BreakpadLogFileTailSize"
#define BREAKPAD_LOGFILE_KEY_PREFIX    "BreakpadAppLogFile"
#define BREAKPAD_EMAIL                 "BreakpadEmail"
#define BREAKPAD_REQUEST_COMMENTS      "BreakpadRequestComments"
#define BREAKPAD_COMMENTS              "BreakpadComments"

// Optional user-defined function to dec to decide if we should handle
// this crash or forward it along.
// Return true if you want Breakpad to handle it.
// Return false if you want Breakpad to skip it
// The exception handler always returns false, as if SEND_AND_EXIT were false
// (which means the next exception handler will take the exception)
typedef bool (*BreakpadFilterCallback)(int exception_type,
                                       int exception_code,
                                       mach_port_t crashing_thread);

// Create a new BreakpadRef object and install it as an
// exception handler.  The |parameters| will typically be the contents
// of your bundle's Info.plist.
//
// You can also specify these additional keys for customizable behavior:
// Key:                           Value:
// BREAKPAD_PRODUCT               Product name (e.g., "MyAwesomeProduct")
//                                This one is used as the key to identify
//                                the product when uploading
//                                REQUIRED
//
// BREAKPAD_PRODUCT_DISPLAY       This is the display name, e.g. a pretty
//                                name for the product when the crash_sender
//                                pops up UI for the user.  Falls back to
//                                BREAKPAD_PRODUCT if not specified.
//
// BREAKPAD_VERSION               Product version (e.g., 1.2.3), used
//                                as metadata for crash report
//                                REQUIRED
//
// BREAKPAD_VENDOR                Vendor named, used in UI (e.g. the Xxxx
//                                foo bar company product widget has crashed)
//
// BREAKPAD_URL                   URL destination for reporting
//                                REQUIRED
//
// BREAKPAD_REPORT_INTERVAL       # of seconds between sending
//                                reports.  If an additional report is
//                                generated within this time, it will
//                                be ignored.  Default: 3600sec.
//                                Specify 0 to send all reports.
//
// BREAKPAD_SKIP_CONFIRM          If true, the reporter will send the report
//                                without any user intervention.
//                                Defaults to NO
//
// BREAKPAD_SEND_AND_EXIT         If true, the handler will exit after sending.
//                                This will prevent any other handler (e.g.,
//                                CrashReporter) from getting the crash.
//                                Defaults TO YES
//
// BREAKPAD_DUMP_DIRECTORY        The directory to store crash-dumps
//                                in. By default, we use
//                                ~/Library/Breakpad/<BREAKPAD_PRODUCT>
//                                The path you specify here is tilde-expanded.
//
// BREAKPAD_INSPECTOR_LOCATION    The full path to the Inspector executable.
//                                Defaults to <Framework resources>/Inspector
//
// BREAKPAD_REPORTER_EXE_LOCATION The full path to the Reporter/sender
//                                executable.
//                                Default:
//                                <Framework Resources>/crash_report_sender.app
//
// BREAKPAD_LOGFILES              Indicates an array of log file paths that
//                                should be uploaded at crash time.
//
// BREAKPAD_REQUEST_COMMENTS      If true, the message dialog will have a
//                                text box for the user to enter comments as
//                                well as a name and email address.
//                                Default: NO
//
// The BREAKPAD_PRODUCT, BREAKPAD_VERSION, and BREAKPAD_URL are
// required to have non-NULL values.  By default, the BREAKPAD_PRODUCT
// will be the CFBundleName and the BREAKPAD_VERSION will be the
// CFBundleVersion when these keys are present in the bundle's
// Info.plist.  If the BREAKPAD_PRODUCT or BREAKPAD_VERSION are
// ultimately undefined, BreakpadCreate() will fail.  You have been
// warned.
//
// If you are running in a debugger, breakpad will not install, unless the
// BREAKPAD_IGNORE_DEBUGGER envionment variable is set and/or non-zero.
//
// The BREAKPAD_SKIP_CONFIRM and BREAKPAD_SEND_AND_EXIT default
// values are NO and YES.  However, they can be controlled by setting their
// values in a user or global plist.
//
// It's easiest to use Breakpad via the Framework, but if you're compiling the
// code in directly, BREAKPAD_INSPECTOR_LOCATION and
// BREAKPAD_REPORTER_EXE_LOCATION allow you to specify custom paths
// to the helper executables.
//

// Returns a new BreakpadRef object on success, NULL otherwise.
BreakpadRef BreakpadCreate(NSDictionary *parameters);

// Uninstall and release the data associated with |ref|.
void BreakpadRelease(BreakpadRef ref);

// Clients may set an optional callback which gets called when a crash occurs.
// The callback function should return |true| if we should handle the crash,
// generate a crash report, etc. or |false| if we should ignore it and forward
// the crash (normally to CrashReporter)
void BreakpadSetFilterCallback(BreakpadRef ref,
                               BreakpadFilterCallback callback);

// User defined key and value string storage
// All set keys will be uploaded with the minidump if a crash occurs
// Keys and Values are limited to 255 bytes (256 - 1 for terminator).
// NB this is BYTES not GLYPHS.
// Anything longer than 255 bytes will be truncated. Note that the string is
// converted to UTF8 before truncation, so any multibyte character that
// straddles the 255 byte limit will be mangled.
//
// A maximum number of 64 key/value pairs are supported.  An assert() will fire
// if more than this number are set.
void BreakpadSetKeyValue(BreakpadRef ref, NSString *key, NSString *value);
NSString *BreakpadKeyValue(BreakpadRef ref, NSString *key);
void BreakpadRemoveKeyValue(BreakpadRef ref, NSString *key);

// Add a log file for Breakpad to read and send upon crash dump
void BreakpadAddLogFile(BreakpadRef ref, NSString *logPathname);

// Generate a minidump and send
void BreakpadGenerateAndSendReport(BreakpadRef ref);

#ifdef __cplusplus
}
#endif
