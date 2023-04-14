#include <c10/core/Device.h>
#include <torch/csrc/Device.h>
#include <torch/csrc/utils/pybind.h>

#include "exportapi.h"
#include <csrc_dipu/runtime/core/DIPUStream.h>
#include <csrc_dipu/runtime/core/DIPUEvent.h>
using dipu::getDIPUStreamFromPool;
using dipu::DIPUStream;
using dipu::DIPUEvent;
namespace py = pybind11;

namespace dipu {

static void exportDevices(py::module& m) {
   // Device Management.
  m.attr("dipu_vendor") = VendorTypeToStr(VENDOR_TYPE);

  m.def("_dipu_set_device", [](int idx) -> void { 
    devapis::setDevice(static_cast<devapis::deviceId_t>(idx)); 
  });
  m.def("_dipu_get_device_count", []() -> int { 
    return devapis::getDeviceCount();
  });
  m.def("_dipu_current_device", []() -> int {
    return static_cast<int>(devapis::current_device()); 
  });
  m.def("_dipu_synchronize", []() -> void { 
    devapis::syncDevice(); 
    return;
  });
}

static void exportStream(py::module& m) {
  // Stream Management. follow the api in torch/csrc/cuda/Stream.cpp
  pybind11::class_<DIPUStream>(m, "_DIPUStreamBase")
    .def(py::init([](int priority, c10::StreamId stream_id, c10::DeviceIndex device_index,
                  int64_t device_type, uint64_t stream_ptr) {
          if (stream_id || device_index || device_type) {
            if (device_type != 0) {
              TORCH_CHECK(static_cast<c10::DeviceType>(device_type) == dipu::DIPU_DEVICE_TYPE);
            }
            return DIPUStream(device_index, stream_id);
          } else if (stream_ptr) {
            return dipu::getStreamFromExternal(reinterpret_cast<deviceStream_t>(stream_ptr),
                                               devapis::current_device());
          } else {
            return getDIPUStreamFromPool();
          }
      }),
      py::arg("priority") = 0, py::arg("stream_id") = 0, py::arg("device_index") = 0,
      py::arg("device_type") = 0, py::arg("stream_ptr")=0
    )
    .def(py::init([](c10::DeviceIndex device_index, int isdefault) {
         return dipu::getCurrentDIPUStream(device_index);
      })
    )
    .def("query", &DIPUStream::isStreamEmpty)
    .def("synchronize",
        [](DIPUStream& stream) -> void {
          pybind11::gil_scoped_release no_gil;
          stream.synchronize();
        })
    .def("__eq__", &DIPUStream::operator==)
    .def("priority_range",
        // not support priority now, return a mock value.
        [](DIPUStream& stream) -> py::tuple {
          py::tuple range = pybind11::make_tuple(0, 0);
          return range;
    })
    // cpp properties 
    .def_property_readonly("stream_id",
        [](DIPUStream& stream) -> c10::StreamId {
          return stream.id();
    })
    .def_property_readonly("device_index", &DIPUStream::device_index)
    .def_property_readonly("device_type",
        [](DIPUStream& stream) -> int64_t {
          return static_cast<int64_t>(stream.device().type());
    })
    .def_property_readonly("dipu_stream",
        [](DIPUStream& stream) -> uint64_t {
          return (uint64_t)stream.rawstream();
    })
    // use type_caster<at::Device>
    .def_property_readonly("device",
        [](DIPUStream& stream) -> at::Device {
          return stream.device();
    });

  m.def("_dipu_setStream", [](c10::StreamId stream_id, c10::DeviceIndex device_index) -> void { 
      dipu::setCurrentDIPUStream(DIPUStream(device_index, stream_id));
    }, py::arg("stream_id") = 0, py::arg("device_index") = 0);

  m.def("_dipu_getCurrentStream", [](c10::DeviceIndex devIdx) -> DIPUStream { 
    return dipu::getCurrentDIPUStream(devIdx);
  });
  m.def("_dipu_getDefaultStream", [](c10::DeviceIndex devIdx) -> DIPUStream {
    return dipu::getDefaultDIPUStream(devIdx);
  });
}

static void exportEvent(py::module& m) {
   // Event
  pybind11::class_<DIPUEvent>(m, "_DIPUEventBase")
      // add flag in future
     .def(py::init([](bool enable_timing, bool blocking, bool interproces) {
         return DIPUEvent();
      }),
      py::arg("enable_timing") = false, py::arg("blocking") = false, py::arg("interprocess") = false
    )
    .def("record", static_cast<void (DIPUEvent::*)()>(&DIPUEvent::record), "record event")
    .def("record", pybind11::overload_cast<const DIPUStream&>
                (&DIPUEvent::record), "record event on stream")
    .def("elapsed_time", &dipu::DIPUEvent::elapsed_time)
    .def("synchronize",
        [](DIPUEvent& self) {
          pybind11::gil_scoped_release no_gil;
          self.synchronize();
        })
    .def("query", &DIPUEvent::query)
    .def("wait",
        [](DIPUEvent& self, const DIPUStream& stream) {
          pybind11::gil_scoped_release no_gil;
          self.wait(stream);
        })
      
    .def_property_readonly("dipu_event", [](DIPUEvent& self) {
          return (uint64_t)self.rawevent();
    })
    .def_property_readonly("device", [](DIPUEvent& self) {
        auto device = self.device().value();
        return device;
    });
}

DIPU_API void exportDIPURuntime(PyObject* module) {
  auto m = py::handle(module).cast<py::module>();
  exportDevices(m);
  exportStream(m);
  exportEvent(m);
 
}
}  // end ns dipu