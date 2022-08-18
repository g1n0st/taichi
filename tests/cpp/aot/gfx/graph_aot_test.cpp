#include "gtest/gtest.h"
#include "taichi/ir/ir_builder.h"
#include "taichi/ir/statements.h"
#include "taichi/inc/constants.h"
#include "taichi/program/program.h"
#include "tests/cpp/ir/ndarray_kernel.h"
#include "tests/cpp/program/test_program.h"
#include "taichi/aot/graph_data.h"
#include "taichi/program/graph_builder.h"
#include "taichi/runtime/gfx/aot_module_loader_impl.h"
#include "tests/cpp/aot/gfx/utils.h"
#ifdef TI_WITH_VULKAN
#include "taichi/rhi/device.h"
#include "taichi/rhi/vulkan/vulkan_device.h"
#include "taichi/rhi/vulkan/vulkan_device_creator.h"
#include "taichi/rhi/vulkan/vulkan_loader.h"
#include "taichi/rhi/vulkan/vulkan_utils.h"
#endif
#ifdef TI_WITH_OPENGL
#include "taichi/rhi/opengl/opengl_api.h"
#endif

using namespace taichi;
using namespace lang;

namespace {
[[maybe_unused]] void run_cgraph2(Arch arch, taichi::lang::Device *device_) {
  // API based on proposal https://github.com/taichi-dev/taichi/issues/3642
  // Initialize program
  taichi::uint64 *result_buffer{nullptr};
  taichi::lang::RuntimeContext host_ctx;
  auto memory_pool = std::make_unique<taichi::lang::MemoryPool>(arch, nullptr);
  result_buffer = (taichi::uint64 *)memory_pool->allocate(
      sizeof(taichi::uint64) * taichi_result_buffer_entries, 8);
  host_ctx.result_buffer = result_buffer;
  // Create runtime
  gfx::GfxRuntime::Params params;
  params.host_result_buffer = result_buffer;
  params.device = device_;
  auto gfx_runtime =
      std::make_unique<taichi::lang::gfx::GfxRuntime>(std::move(params));

  // Run AOT module loader
  const auto folder_dir = getenv("TAICHI_AOT_FOLDER_PATH");
  std::stringstream ss;
  ss << folder_dir;
  gfx::AotModuleParams mod_params;
  mod_params.module_path = ss.str();
  mod_params.runtime = gfx_runtime.get();

  std::unique_ptr<aot::Module> module = aot::Module::load(arch, mod_params);
  EXPECT_TRUE(module);

  // Retrieve kernels/fields/etc from AOT module
  auto root_size = module->get_root_size();
  EXPECT_EQ(root_size, 0);
  gfx_runtime->add_root_buffer(root_size);

  auto graph = module->get_graph("test");

  const int size = 10;
  taichi::lang::Device::AllocParams alloc_params;
  alloc_params.host_write = true;
  alloc_params.host_read = true;
  alloc_params.size = size * sizeof(int);
  alloc_params.usage = taichi::lang::AllocUsage::Storage;
  DeviceAllocation devalloc_arr_ = device_->allocate_memory(alloc_params);

  int src[size] = {0};
  src[0] = 2;
  src[2] = 40;
  write_devalloc(devalloc_arr_, src, sizeof(src));

  std::unordered_map<std::string, aot::IValue> args;
  auto arr = Ndarray(devalloc_arr_, PrimitiveType::i32, {size});
  args.insert({"arr", aot::IValue::create(arr)});
  args.insert({"x", aot::IValue::create<int>(2)});
  graph->run(args);
  gfx_runtime->synchronize();

  int dst[size] = {1};
  load_devalloc(devalloc_arr_, dst, sizeof(dst));

  EXPECT_EQ(dst[0], 2);
  EXPECT_EQ(dst[1], 2);
  EXPECT_EQ(dst[2], 42);
  device_->dealloc_memory(devalloc_arr_);
}

[[maybe_unused]] void run_cgraph1(Arch arch, taichi::lang::Device *device_) {
  // API based on proposal https://github.com/taichi-dev/taichi/issues/3642
  // Initialize  program
  taichi::uint64 *result_buffer{nullptr};
  taichi::lang::RuntimeContext host_ctx;
  auto memory_pool = std::make_unique<taichi::lang::MemoryPool>(arch, nullptr);
  result_buffer = (taichi::uint64 *)memory_pool->allocate(
      sizeof(taichi::uint64) * taichi_result_buffer_entries, 8);
  host_ctx.result_buffer = result_buffer;
  // Create runtime
  gfx::GfxRuntime::Params params;
  params.host_result_buffer = result_buffer;
  params.device = device_;
  auto gfx_runtime =
      std::make_unique<taichi::lang::gfx::GfxRuntime>(std::move(params));

  // Run AOT module loader
  const auto folder_dir = getenv("TAICHI_AOT_FOLDER_PATH");
  std::stringstream ss;
  ss << folder_dir;
  gfx::AotModuleParams mod_params;
  mod_params.module_path = ss.str();
  mod_params.runtime = gfx_runtime.get();

  std::unique_ptr<aot::Module> module = aot::Module::load(arch, mod_params);
  EXPECT_TRUE(module);

  // Retrieve kernels/fields/etc from AOT module
  auto root_size = module->get_root_size();
  EXPECT_EQ(root_size, 0);
  gfx_runtime->add_root_buffer(root_size);

  // Prepare Ndarray for model
  constexpr int size = 100;
  taichi::lang::Device::AllocParams alloc_params;
  alloc_params.host_write = false;
  alloc_params.host_read = true;
  alloc_params.size = size * sizeof(int);
  alloc_params.usage = taichi::lang::AllocUsage::Storage;
  DeviceAllocation devalloc_arr_ = device_->allocate_memory(alloc_params);
  Ndarray arr = Ndarray(devalloc_arr_, PrimitiveType::i32, {size}, {1});

  int base0 = 10;
  int base1 = 20;
  int base2 = 30;

  std::unordered_map<std::string, taichi::lang::aot::IValue> args;
  args.insert({"arr", taichi::lang::aot::IValue::create(arr)});
  args.insert({"base0", taichi::lang::aot::IValue::create(base0)});
  args.insert({"base1", taichi::lang::aot::IValue::create(base1)});
  args.insert({"base2", taichi::lang::aot::IValue::create(base2)});

  // Prepare & Run "init" Graph
  auto graph = module->get_graph("run_graph");
  graph->run(args);
  gfx_runtime->synchronize();

  int dst[size] = {0};
  load_devalloc(devalloc_arr_, dst, sizeof(dst));
  for (int i = 0; i < size; i++) {
    EXPECT_EQ(dst[i], 3 * i + base0 + base1 + base2);
  }

  // Deallocate
  device_->dealloc_memory(devalloc_arr_);
}
}  // namespace
#ifdef TI_WITH_VULKAN
TEST(CGraphAotTest, VulkanRunCGraph2) {
  // Otherwise will segfault on macOS VM,
  // where Vulkan is installed but no devices are present
  if (!vulkan::is_vulkan_api_available()) {
    return;
  }

  // Create Taichi Device for computation
  lang::vulkan::VulkanDeviceCreator::Params evd_params;
  evd_params.api_version = std::nullopt;
  auto embedded_device =
      std::make_unique<taichi::lang::vulkan::VulkanDeviceCreator>(evd_params);
  taichi::lang::vulkan::VulkanDevice *device_ =
      static_cast<taichi::lang::vulkan::VulkanDevice *>(
          embedded_device->device());
  run_cgraph2(Arch::vulkan, device_);
}

TEST(CGraphAotTest, VulkanRunCGraph1) {
  // Otherwise will segfault on macOS VM,
  // where Vulkan is installed but no devices are present
  if (!vulkan::is_vulkan_api_available()) {
    return;
  }

  // Create Taichi Device for computation
  lang::vulkan::VulkanDeviceCreator::Params evd_params;
  evd_params.api_version = std::nullopt;
  auto embedded_device =
      std::make_unique<taichi::lang::vulkan::VulkanDeviceCreator>(evd_params);
  taichi::lang::vulkan::VulkanDevice *device_ =
      static_cast<taichi::lang::vulkan::VulkanDevice *>(
          embedded_device->device());
  run_cgraph1(Arch::vulkan, device_);
}

[[maybe_unused]] void run_mpm88_graph(Arch arch,
                                      taichi::lang::Device *device_) {
  constexpr int NR_PARTICLES = 8192 * 5;
  constexpr int N_GRID = 128;

  // API based on proposal https://github.com/taichi-dev/taichi/issues/3642
  // Initialize program
  taichi::uint64 *result_buffer{nullptr};
  taichi::lang::RuntimeContext host_ctx;
  auto memory_pool = std::make_unique<taichi::lang::MemoryPool>(arch, nullptr);
  result_buffer = (taichi::uint64 *)memory_pool->allocate(
      sizeof(taichi::uint64) * taichi_result_buffer_entries, 8);
  host_ctx.result_buffer = result_buffer;
  // Create runtime
  gfx::GfxRuntime::Params params;
  params.host_result_buffer = result_buffer;
  params.device = device_;
  auto gfx_runtime =
      std::make_unique<taichi::lang::gfx::GfxRuntime>(std::move(params));

  // Run AOT module loader
  const auto folder_dir = getenv("TAICHI_AOT_FOLDER_PATH");
  std::stringstream ss;
  ss << folder_dir;
  gfx::AotModuleParams mod_params;
  mod_params.module_path = ss.str();
  mod_params.runtime = gfx_runtime.get();

  std::unique_ptr<aot::Module> module = aot::Module::load(arch, mod_params);
  EXPECT_TRUE(module);

  // Retrieve kernels/fields/etc from AOT module
  auto root_size = module->get_root_size();
  EXPECT_EQ(root_size, 0);
  gfx_runtime->add_root_buffer(root_size);

  auto g_init = module->get_graph("init");
  auto g_update = module->get_graph("update");

  // Prepare Ndarray for model
  taichi::lang::Device::AllocParams alloc_params;
  alloc_params.host_write = false;
  alloc_params.host_read = false;
  alloc_params.size = NR_PARTICLES * 2 * sizeof(float);
  alloc_params.usage = taichi::lang::AllocUsage::Storage;

  taichi::lang::DeviceAllocation devalloc_x =
      device_->allocate_memory(alloc_params);
  auto x = taichi::lang::Ndarray(devalloc_x, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2});

  taichi::lang::DeviceAllocation devalloc_v =
      device_->allocate_memory(alloc_params);
  auto v = taichi::lang::Ndarray(devalloc_v, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2});

  alloc_params.size = NR_PARTICLES * 3 * sizeof(float);
  taichi::lang::DeviceAllocation devalloc_pos =
      device_->allocate_memory(alloc_params);
  auto pos = taichi::lang::Ndarray(
      devalloc_pos, taichi::lang::PrimitiveType::f32, {NR_PARTICLES}, {3});

  alloc_params.size = NR_PARTICLES * sizeof(float) * 2 * 2;
  taichi::lang::DeviceAllocation devalloc_C =
      device_->allocate_memory(alloc_params);
  auto C = taichi::lang::Ndarray(devalloc_C, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES}, {2, 2});

  alloc_params.size = NR_PARTICLES * sizeof(float);
  taichi::lang::DeviceAllocation devalloc_J =
      device_->allocate_memory(alloc_params);
  auto J = taichi::lang::Ndarray(devalloc_J, taichi::lang::PrimitiveType::f32,
                                 {NR_PARTICLES});

  alloc_params.size = N_GRID * N_GRID * 2 * sizeof(float);
  taichi::lang::DeviceAllocation devalloc_grid_v =
      device_->allocate_memory(alloc_params);
  auto grid_v = taichi::lang::Ndarray(
      devalloc_grid_v, taichi::lang::PrimitiveType::f32, {N_GRID, N_GRID}, {2});

  alloc_params.size = N_GRID * N_GRID * sizeof(float);
  taichi::lang::DeviceAllocation devalloc_grid_m =
      device_->allocate_memory(alloc_params);
  auto grid_m = taichi::lang::Ndarray(
      devalloc_grid_m, taichi::lang::PrimitiveType::f32, {N_GRID, N_GRID});

  std::unordered_map<std::string, taichi::lang::aot::IValue> args;
  args.insert({"x", taichi::lang::aot::IValue::create(x)});
  args.insert({"v", taichi::lang::aot::IValue::create(v)});
  args.insert({"J", taichi::lang::aot::IValue::create(J)});

  g_init->run(args);
  gfx_runtime->synchronize();

  args.insert({"C", taichi::lang::aot::IValue::create(C)});
  args.insert({"grid_v", taichi::lang::aot::IValue::create(grid_v)});
  args.insert({"grid_m", taichi::lang::aot::IValue::create(grid_m)});
  args.insert({"pos", taichi::lang::aot::IValue::create(pos)});

  // Run update graph once. In real application this runs as long as window is
  // alive.
  g_update->run(args);
  gfx_runtime->synchronize();

  device_->dealloc_memory(devalloc_x);
  device_->dealloc_memory(devalloc_v);
  device_->dealloc_memory(devalloc_J);
  device_->dealloc_memory(devalloc_C);
  device_->dealloc_memory(devalloc_grid_v);
  device_->dealloc_memory(devalloc_grid_m);
  device_->dealloc_memory(devalloc_pos);
}

TEST(CGraphAotTest, VulkanMpm88) {
  // Otherwise will segfault on macOS VM,
  // where Vulkan is installed but no devices are present
  if (!vulkan::is_vulkan_api_available()) {
    return;
  }

  // Create Taichi Device for computation
  lang::vulkan::VulkanDeviceCreator::Params evd_params;
  evd_params.api_version = std::nullopt;
  auto embedded_device =
      std::make_unique<taichi::lang::vulkan::VulkanDeviceCreator>(evd_params);
  taichi::lang::vulkan::VulkanDevice *device_ =
      static_cast<taichi::lang::vulkan::VulkanDevice *>(
          embedded_device->device());
  run_mpm88_graph(Arch::vulkan, device_);
}

#endif
#ifdef TI_WITH_OPENGL
TEST(CGraphAotTest, OpenglRunCGraph1) {
  if (!opengl::is_opengl_api_available()) {
    return;
  }

  auto device = taichi::lang::opengl::make_opengl_device();
  run_cgraph1(Arch::opengl, device.get());
}

TEST(CGraphAotTest, OpenglRunCGraph2) {
  if (!opengl::is_opengl_api_available()) {
    return;
  }

  auto device = taichi::lang::opengl::make_opengl_device();
  run_cgraph2(Arch::opengl, device.get());
}

TEST(CGraphAotTest, OpenglMpm88) {
  if (!opengl::is_opengl_api_available()) {
    return;
  }

  auto device = taichi::lang::opengl::make_opengl_device();
  run_mpm88_graph(Arch::opengl, device.get());
}
#endif