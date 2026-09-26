#include "layer/Layer.h"

#include <catch2/catch_test_macros.hpp>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

using namespace poselayer;

namespace {

XrNegotiateLoaderInfo goodLoaderInfo() {
    XrNegotiateLoaderInfo info = {};
    info.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    info.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    info.structSize = sizeof(XrNegotiateLoaderInfo);
    info.minInterfaceVersion = 1;
    info.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    info.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.maxApiVersion = XR_CURRENT_API_VERSION;
    return info;
}

XrNegotiateApiLayerRequest emptyRequest() {
    XrNegotiateApiLayerRequest req = {};
    req.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    req.structVersion = XR_API_LAYER_INFO_STRUCT_VERSION;
    req.structSize = sizeof(XrNegotiateApiLayerRequest);
    return req;
}

} // namespace

TEST_CASE("negotiate fills the request with the layer entry points", "[negotiation]") {
    const XrNegotiateLoaderInfo info = goodLoaderInfo();
    XrNegotiateApiLayerRequest req = emptyRequest();

    REQUIRE(negotiate(&info, "XR_APILAYER_GRC_pose_layer", &req) == XR_SUCCESS);
    CHECK(req.layerInterfaceVersion == XR_CURRENT_LOADER_API_LAYER_VERSION);
    CHECK(req.layerApiVersion == XR_CURRENT_API_VERSION);
    CHECK(req.getInstanceProcAddr != nullptr);
    CHECK(req.createApiLayerInstance != nullptr);
}

TEST_CASE("negotiate rejects null arguments", "[negotiation]") {
    XrNegotiateApiLayerRequest req = emptyRequest();
    const XrNegotiateLoaderInfo info = goodLoaderInfo();
    CHECK(negotiate(nullptr, "x", &req) == XR_ERROR_INITIALIZATION_FAILED);
    CHECK(negotiate(&info, "x", nullptr) == XR_ERROR_INITIALIZATION_FAILED);
}

TEST_CASE("negotiate rejects a wrong loader struct type", "[negotiation]") {
    XrNegotiateLoaderInfo info = goodLoaderInfo();
    info.structType = XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED;
    XrNegotiateApiLayerRequest req = emptyRequest();
    CHECK(negotiate(&info, "x", &req) == XR_ERROR_INITIALIZATION_FAILED);
}

TEST_CASE("negotiate rejects an interface version it cannot meet", "[negotiation]") {
    XrNegotiateLoaderInfo info = goodLoaderInfo();
    info.minInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION + 1;
    info.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION + 1;
    XrNegotiateApiLayerRequest req = emptyRequest();
    CHECK(negotiate(&info, "x", &req) == XR_ERROR_INITIALIZATION_FAILED);
}

TEST_CASE("negotiate rejects a bad request struct size", "[negotiation]") {
    const XrNegotiateLoaderInfo info = goodLoaderInfo();
    XrNegotiateApiLayerRequest req = emptyRequest();
    req.structSize = 1;
    CHECK(negotiate(&info, "x", &req) == XR_ERROR_INITIALIZATION_FAILED);
}
