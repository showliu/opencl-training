//Matrix multiply example
//Author: Ugo Varetto
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <cmath>
#include <sstream>
#include "clutil.h"

#ifdef USE_DOUBLE
typedef double real_t;
#else
typedef float real_t;
#endif

//------------------------------------------------------------------------------
std::vector< real_t > create_matrix(int cols, int rows) {
	std::vector< real_t > m(cols * rows);
	srand(time(0));
	for(std::vector<real_t>::iterator i = m.begin();
	    i != m.end(); ++i) *i = rand() % 10; 
	return m;
}


//------------------------------------------------------------------------------
void host_matmul(const std::vector< real_t >& A,
	             const std::vector< real_t >& B,
	             std::vector< real_t >& C, 
	             int a_columns,
	             int b_columns) {
	const int rows = a_columns;
	const int columns = b_columns;
	for(int r = 0; r != rows; ++r) {
		for(int c = 0; c != columns; ++c) {
			C[r*b_columns + c] = 0;
			for(int ic = 0; ic != a_columns; ++ic) {
				C[r*b_columns + c] += A[r*a_columns + ic]
				                      * B[ic*b_columns + c];
			}
		}
	}
}

//------------------------------------------------------------------------------
bool check_result(const std::vector< real_t >& v1,
	              const std::vector< real_t >& v2,
	              double eps) {
    for(int i = 0; i != v1.size(); ++i) {
    	if(double(std::fabs(v1[i] - v2[i])) > eps) return false;
    }
    return true;
}

//------------------------------------------------------------------------------
int main(int argc, char** argv) {
    if(argc < 6) {
        std::cerr << "usage: " << argv[0]
                  << " <platform name> <device type = default | cpu | gpu "
                     "| acc | all>  <device num> <OpenCL source file path>"
                     " <kernel name>"
                  << std::endl;
        exit(EXIT_FAILURE);   
    }
    const int SIZE = 16; //16 x 16
    const size_t BYTE_SIZE = SIZE * SIZE * sizeof(real_t);
    const int BLOCK_SIZE = 4; //4 x 4 tiles
    //setup text header that will be prefixed to opencl code
    std::ostringstream clheaderStream;
    clheaderStream << "#define BLOCK_SIZE " << BLOCK_SIZE << '\n';
#ifdef USE_DOUBLE    
    clheaderStream << "#define DOUBLE\n";
    const double EPS = 0.000000001;
#else
    const double EPS = 0.00001;
#endif    
    CLEnv clenv = create_clenv(argv[1], argv[2], atoi(argv[3]), false,
                               argv[4], argv[5], clheaderStream.str());
   
    cl_int status;
    //create input and output matrices
    std::vector<real_t> A = create_matrix(SIZE, SIZE);
    std::vector<real_t> B = create_matrix(SIZE, SIZE);
    std::vector<real_t> C(SIZE * SIZE,real_t(0));
    std::vector<real_t> refC(SIZE * SIZE,real_t(0));        
    
    //allocate output buffer on OpenCL device
    cl_mem devC = clCreateBuffer(clenv.context,
                                 CL_MEM_WRITE_ONLY,
                                 BYTE_SIZE,
                                 0,
                                 &status);
    check_cl_error(status, "clCreateBuffer");

    //allocate input buffers on OpenCL devices and copy data
    cl_mem devA = clCreateBuffer(clenv.context,
                                 CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 BYTE_SIZE,
                                 &A[0], //<-- copy data from A
                                 &status);
    check_cl_error(status, "clCreateBuffer");                              
    cl_mem devB = clCreateBuffer(clenv.context,
                                 CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 BYTE_SIZE,
                                 &B[0], //<-- copy data from B
                                 &status);
    check_cl_error(status, "clCreateBuffer");                              

    //set kernel parameters
    status = clSetKernelArg(clenv.kernel, //kernel
                            0,      //parameter id
                            sizeof(cl_mem), //size of parameter
                            &devA); //pointer to parameter
    check_cl_error(status, "clSetKernelArg(A)");
    status = clSetKernelArg(clenv.kernel, //kernel
                            1,      //parameter id
                            sizeof(cl_mem), //size of parameter
                            &devB); //pointer to parameter
    check_cl_error(status, "clSetKernelArg(B)");
    status = clSetKernelArg(clenv.kernel, //kernel
                            2,      //parameter id
                            sizeof(cl_mem), //size of parameter
                            &devC); //pointer to parameter
    check_cl_error(status, "clSetKernelArg(C)");
    status = clSetKernelArg(clenv.kernel, //kernel
                            3,      //parameter id
                            sizeof(int), //size of parameter
                            &SIZE); //pointer to parameter
    check_cl_error(status, "clSetKernelArg(SIZE)");


    //setup kernel launch configuration
    //total number of threads == number of array elements
    const size_t globalWorkSize[2] = {SIZE, SIZE};
    //number of per-workgroup local threads
    const size_t localWorkSize[2] = {BLOCK_SIZE, BLOCK_SIZE}; 

    //launch kernel
    status = clEnqueueNDRangeKernel(clenv.commandQueue, //queue
                                    clenv.kernel, //kernel                                   
                                    2, //number of dimensions for work-items
                                    0, //global work offset
                                    globalWorkSize, //total number of threads
                                    localWorkSize, //threads per workgroup
                                    0, //number of events that need to
                                       //complete before kernel executed
                                    0, //list of events that need to complete
                                       //before kernel executed
                                    0); //event object identifying this
                                        //particular kernel execution instance

    check_cl_error(status, "clEnqueueNDRangeKernel");
    
    //read back and check results
    status = clEnqueueReadBuffer(clenv.commandQueue,
                                 devC,
                                 CL_TRUE, //blocking read
                                 0, //offset
                                 BYTE_SIZE, //byte size of data
                                 &C[0], //destination buffer in host memory
                                 0, //number of events that need to
                                    //complete before transfer executed
                                 0, //list of events that need to complete
                                    //before transfer executed
                                 0); //event identifying this specific operation
    check_cl_error(status, "clEnqueueReadBuffer");
    
    host_matmul(A, B, refC, SIZE, SIZE);

    if(check_result(refC, C, EPS)) {
    	std::cout << "PASSED" << std::endl;
    } else {
    	std::cout << "FAILED" << std::endl;
    }	

    check_cl_error(clReleaseMemObject(devA), "clReleaseMemObject");
    check_cl_error(clReleaseMemObject(devB), "clReleaseMemObject");
    check_cl_error(clReleaseMemObject(devC), "clReleaseMemObject");
    release_clenv(clenv);
   
    return 0;
}