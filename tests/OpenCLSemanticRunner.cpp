#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct OpenCLRuntime
{
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_device_id device = nullptr;

    ~OpenCLRuntime()
    {
        if (queue != nullptr)
        {
            clReleaseCommandQueue(queue);
        }
        if (context != nullptr)
        {
            clReleaseContext(context);
        }
    }
};

bool check(cl_int result, const char* operation)
{
    if (result == CL_SUCCESS)
    {
        return true;
    }

    std::cerr << operation << " failed with OpenCL error " << result << '\n';
    return false;
}

std::string readSource(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        std::cerr << "Cannot open OpenCL source '" << path << "'.\n";
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool initializeRuntime(OpenCLRuntime& runtime)
{
    cl_uint platformCount = 0;
    if (!check(clGetPlatformIDs(0, nullptr, &platformCount), "clGetPlatformIDs") ||
        platformCount == 0)
    {
        std::cerr << "No OpenCL platform is available.\n";
        return false;
    }

    std::vector<cl_platform_id> platforms(platformCount);
    if (!check(
            clGetPlatformIDs(platformCount, platforms.data(), nullptr),
            "clGetPlatformIDs"))
    {
        return false;
    }

    cl_platform_id selectedPlatform = nullptr;
    for (cl_platform_id platform : platforms)
    {
        cl_uint deviceCount = 0;
        const cl_int result = clGetDeviceIDs(
            platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &deviceCount);
        if (result == CL_SUCCESS && deviceCount > 0)
        {
            std::vector<cl_device_id> devices(deviceCount);
            if (!check(
                    clGetDeviceIDs(
                        platform, CL_DEVICE_TYPE_ALL, deviceCount,
                        devices.data(), nullptr),
                    "clGetDeviceIDs"))
            {
                return false;
            }

            selectedPlatform = platform;
            runtime.device = devices.front();
            break;
        }
    }

    if (selectedPlatform == nullptr || runtime.device == nullptr)
    {
        std::cerr << "No OpenCL device is available.\n";
        return false;
    }

    const cl_context_properties properties[] = {
        CL_CONTEXT_PLATFORM,
        reinterpret_cast<cl_context_properties>(selectedPlatform),
        0,
    };

    cl_int result = CL_SUCCESS;
    runtime.context = clCreateContext(
        properties, 1, &runtime.device, nullptr, nullptr, &result);
    if (!check(result, "clCreateContext"))
    {
        return false;
    }

    runtime.queue = clCreateCommandQueue(
        runtime.context, runtime.device, 0, &result);
    return check(result, "clCreateCommandQueue");
}

void printBuildLog(cl_program program, cl_device_id device)
{
    std::size_t logSize = 0;
    if (clGetProgramBuildInfo(
            program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr,
            &logSize) != CL_SUCCESS ||
        logSize == 0)
    {
        return;
    }

    std::string log(logSize, '\0');
    if (clGetProgramBuildInfo(
            program, device, CL_PROGRAM_BUILD_LOG, log.size(),
            &log[0], nullptr) == CL_SUCCESS)
    {
        std::cerr << log << '\n';
    }
}

bool executeKernel(
    const OpenCLRuntime& runtime,
    const char* sourcePath,
    std::vector<cl_int>& values)
{
    const std::string source = readSource(sourcePath);
    if (source.empty())
    {
        return false;
    }

    const char* sourcePointer = source.c_str();
    const std::size_t sourceLength = source.size();
    cl_int result = CL_SUCCESS;
    cl_program program = clCreateProgramWithSource(
        runtime.context, 1, &sourcePointer, &sourceLength, &result);
    if (!check(result, "clCreateProgramWithSource"))
    {
        return false;
    }

    result = clBuildProgram(
        program, 1, &runtime.device, "-cl-std=CL1.2", nullptr, nullptr);
    if (result != CL_SUCCESS)
    {
        check(result, "clBuildProgram");
        printBuildLog(program, runtime.device);
        clReleaseProgram(program);
        return false;
    }

    cl_kernel kernel = clCreateKernel(program, "semantic_kernel", &result);
    if (!check(result, "clCreateKernel"))
    {
        clReleaseProgram(program);
        return false;
    }

    cl_mem buffer = clCreateBuffer(
        runtime.context,
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        values.size() * sizeof(values[0]),
        values.data(),
        &result);
    if (!check(result, "clCreateBuffer"))
    {
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        return false;
    }

    bool success = check(
        clSetKernelArg(kernel, 0, sizeof(buffer), &buffer),
        "clSetKernelArg");
    const std::size_t globalWorkSize = values.size();
    if (success)
    {
        success = check(
            clEnqueueNDRangeKernel(
                runtime.queue, kernel, 1, nullptr, &globalWorkSize,
                nullptr, 0, nullptr, nullptr),
            "clEnqueueNDRangeKernel");
    }
    if (success)
    {
        success = check(clFinish(runtime.queue), "clFinish");
    }
    if (success)
    {
        success = check(
            clEnqueueReadBuffer(
                runtime.queue, buffer, CL_TRUE, 0,
                values.size() * sizeof(values[0]), values.data(),
                0, nullptr, nullptr),
            "clEnqueueReadBuffer");
    }

    clReleaseMemObject(buffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    return success;
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: OpenCLSemanticRunner <original.cl> <obfuscated.cl>\n";
        return 2;
    }

    OpenCLRuntime runtime;
    if (!initializeRuntime(runtime))
    {
        return 3;
    }

    const std::vector<cl_int> input = {-4, 0, 3, 9, 10, 15, 42};
    const std::vector<cl_int> expected = {-11, 1, 10, 28, 8, 13, 40};
    std::vector<cl_int> originalResult = input;
    std::vector<cl_int> obfuscatedResult = input;

    if (!executeKernel(runtime, argv[1], originalResult) ||
        !executeKernel(runtime, argv[2], obfuscatedResult))
    {
        return 4;
    }

    if (originalResult != expected)
    {
        std::cerr << "The original OpenCL kernel produced an unexpected result.\n";
        return 5;
    }

    if (obfuscatedResult != originalResult)
    {
        std::cerr << "OpenCL behavior changed after obfuscation.\n";
        return 6;
    }

    std::cout << "PASS\n";
    return 0;
}
