/* Issues with __local pointers (lp:918801)

   Copyright (c) 2012 Pekka Jääskeläinen / Tampere University of Technology

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

// Enable OpenCL C++ exceptions
#define CL_HPP_ENABLE_EXCEPTIONS

#include <CL/opencl.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "pocl_opencl.h"

#define WORK_ITEMS 2
#define BUFFER_SIZE (WORK_ITEMS)

static char kernelSourceCode[] = R"OPENCLC(
kernel void test_kernel (global float *a, local int *local_buf, private int scalar)
{
   int lid = get_local_id(0);
   local int automatic_local_scalar;
   local int automatic_local_buf[2];

   __local int *p;

   p = automatic_local_buf;
   p[lid] = lid + scalar;
   p = local_buf;
   p[lid] = a[lid];
   automatic_local_scalar = scalar;

   barrier(CLK_LOCAL_MEM_FENCE);

   a[lid] = automatic_local_buf[lid] + local_buf[lid] + automatic_local_scalar;
}
)OPENCLC";

int
main(void)
{
    float A[BUFFER_SIZE];
    bool success = false;

    std::vector<cl::Platform> platformList;
    try {

        // Pick platform
        cl::Platform::get(&platformList);

        // Pick first platform
        cl_context_properties cprops[] = {
            CL_CONTEXT_PLATFORM, (cl_context_properties)(platformList[0])(), 0};
        cl::Context context(CL_DEVICE_TYPE_ALL, cprops);

        // Query the set of devices attched to the context
        std::vector<cl::Device> devices = context.getInfo<CL_CONTEXT_DEVICES>();

        // Create and program from source
        cl::Program::Sources sources({kernelSourceCode});
        cl::Program program(context, sources);

        cl_device_id dev_id = devices.at(0)();

        int scalar = poclu_bswap_cl_int (dev_id, 4);

        for (int i = 0; i < BUFFER_SIZE; ++i)
            A[i] = poclu_bswap_cl_float(dev_id, (cl_float)i);

        // Build program
        program.build(devices);

        cl::Buffer aBuffer =
            cl::Buffer(context, CL_MEM_COPY_HOST_PTR,
                       BUFFER_SIZE * sizeof(float), (void *)&A[0]);

        // Create kernel object
        cl::Kernel kernel(program, "test_kernel");

        // Set kernel args
        kernel.setArg(0, aBuffer);
        kernel.setArg(1, (BUFFER_SIZE * sizeof(int)), NULL);
        kernel.setArg(2, scalar);

        // Create command queue
        cl::CommandQueue queue(context, devices[0], 0);

        // Do the work
        queue.enqueueNDRangeKernel(kernel, cl::NullRange,
                                   cl::NDRange(WORK_ITEMS),
                                   cl::NDRange(WORK_ITEMS));

        // Map aBuffer to host pointer. This enforces a sync with 
        // the host backing space, remember we choose GPU device.
        float *res = (float *)queue.enqueueMapBuffer(
            aBuffer,
            CL_TRUE, // block
            CL_MAP_READ, 0, BUFFER_SIZE * sizeof(float));

        res[0] = poclu_bswap_cl_float (dev_id, res[0]);
        res[1] = poclu_bswap_cl_float (dev_id, res[1]);
        success = (res[0] == 8 && res[1] == 10);
        if (!success) {
            std::cout << "FAIL: " << res[0] << " " << res[1] << std::endl;
            std::cout << "res@" << std::hex << res << std::endl;
            std::cout << "A@" << std::hex << A << std::endl;
        }

        // Finally release our hold on accessing the memory
        queue.enqueueUnmapMemObject(
            aBuffer, (void *) res);
        queue.finish();
    }
    catch (cl::Error &err) {
        std::cerr << "ERROR: " << err.what() << "(" << err.err() << ")"
                  << std::endl;
        return EXIT_FAILURE;
    }

    platformList[0].unloadCompiler();

    if (success) {
        std::cout << "OK" << std::endl;
        return EXIT_SUCCESS;
    } else {
        std::cout << "FAIL\n";
        return EXIT_FAILURE;
    }
}
