#define NOMINMAX

#include <string>
#include "mwcas_benchmark.h"
#include "MSQueue.h"


using namespace pmwcas::benchmark;

DEFINE_uint64(node_size, 1048576, "");
DEFINE_uint64(queue_impl_type, 0, "");
DEFINE_uint64(queue_op_type, 0, "");

namespace pmwcas {

struct MSQueueTest : public BaseMwCas {
    void Setup(size_t thread_count) {










    }

    void Main(size_t thread_index) {















    }




    void Teardown() {




















    }


};





}

//////////////////////////////////////////////////////////////////////////////////////////////////
using namespace pmwcas;

void doTest(MSQueueTest & test) {
    std::cout << "Starting MSQueue benchmark..." << std::endl;
    test.Run(FLAGS_threads, FLAGS_seconds,
        static_cast<AffinityPattern>(FLAGS_affinity),
        FLAGS_metrics_dump_interval);
    printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),  (double)test.GetOperationCount() / test.GetRunSeconds());

    double opPerSec = (double)test.GetTotalSuccess() / test.GetRunSeconds();
    if(FLAGS_queue_op_type == 0) {
        printf("enq 50 percent and deq 50 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	        (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
    else if(FLAGS_queue_op_type == 1) {
        printf("enq 90 percent and deq 10 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	         (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
    else if(FLAGS_queue_op_type == 2) {
        printf("enq 10 percent and deq 90 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	         (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
}

void runBenchmark() {
    DumpArgs();
    std::cout << "> Args node_size " << FLAGS_node_size << std::endl;
    std::cout << "> Args queue_impl_type " << FLAGS_queue_impl_type << std::endl;
    std::cout << "> Args queue_op_type " << FLAGS_queue_op_type << std::endl;

    MSQueueTest test;
    doTest(test);

    



}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
  
    FLAGS_log_dir = "./";
  
#ifdef WIN32
    return;
#else
  pmwcas::InitLibrary(pmwcas::TlsAllocator::Create,
                      pmwcas::TlsAllocator::Destroy,
                      pmwcas::LinuxEnvironment::Create,
                      pmwcas::LinuxEnvironment::Destroy);
#endif
  runBenchmark();
  return 0;
}

