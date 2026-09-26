// The one symbol the OpenXR loader looks up in the layer DLL by name. It
// forwards to poselayer::negotiate; everything else the loader reaches through
// the function pointers that call fills in.
#include "layer/Layer.h"

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                                                             const char* layerName,
                                                                             XrNegotiateApiLayerRequest* apiLayerRequest) {
    return poselayer::negotiate(loaderInfo, layerName, apiLayerRequest);
}
