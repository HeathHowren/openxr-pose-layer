// The layer's public surface for the loader entry points and the tests.
//
// negotiate() and createApiLayerInstance() are the two functions the loader
// reaches first; the exported C entry point in Negotiation.cpp forwards to
// negotiate(). getInstanceProcAddr() is what the loader (and the tests) call to
// fetch every hooked function. setConfigOverride() lets a test pin a Config
// instead of reading the environment.
#pragma once

#include "core/Config.h"

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

namespace poselayer {

XrResult negotiate(const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* apiLayerRequest);

XRAPI_ATTR XrResult XRAPI_CALL createApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* apiLayerInfo,
                                                      XrInstance* instance);

XRAPI_ATTR XrResult XRAPI_CALL getInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

namespace testing {
// Pin the Config the next created instance will use, in place of the
// environment. Pass nullptr to go back to reading the environment. For tests.
void setConfigOverride(const Config* config);
} // namespace testing

} // namespace poselayer
