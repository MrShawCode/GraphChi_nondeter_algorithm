#include <cmath>
#include <string>

#include "graphchi_basic_includes.hpp"
#include "util/toplist.hpp"

using namespace graphchi;

int         iterationcount = 0;
bool        scheduler = false;

/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program. 
 */
#define THRESHOLD 1e-1    
#define RANDOMRESETPROB 0.15

typedef float VertexDataType;
typedef float PreEdgeDataType;
typedef std::atomic<float> EdgeDataType;

float epsilon = 0.5;

/**
 * GraphChi programs need to subclass GraphChiProgram<vertex-type, edge-type> 
 * class. The main logic is usually in the update function.
 */
struct PagerankConverge: public GraphChiProgram<VertexDataType, EdgeDataType,PreEdgeDataType> {
    
    bool converged;
    
    void update(graphchi_vertex<VertexDataType, EdgeDataType, PreEdgeDataType> &vertex, graphchi_context &gcontext) {
        
        if (scheduler) gcontext.scheduler->remove_tasks(vertex.id(), vertex.id());
        float sum=0;
        if (gcontext.iteration == 0) {
			for( int i=0; i<vertex.num_outedges(); i++) {
				graphchi_edge<EdgeDataType,PreEdgeDataType>* edge=vertex.outedge(i);
				edge->set_data( 1.0/vertex.num_outedges() );
			}
			vertex.set_data( RANDOMRESETPROB );
					
            if (scheduler)  gcontext.scheduler->add_task(vertex.id());
        }else{
			float old_value = vertex.get_data();
            /* Compute the sum of neighbors' weighted pageranks by
               reading from the in-edges. */
            for(int i=0; i < vertex.num_inedges(); i++) {
                float val = vertex.inedge(i)->get_data();
                sum += val;                    
            }
            
            /* Compute my pagerank */ 
			float pagerank = RANDOMRESETPROB + (1 - RANDOMRESETPROB) * sum;
            

            /* Write my pagerank divided by the number of out-edges to
               each of my out-edges. */
            if (vertex.num_outedges() > 0) {
                float pagerankcont = pagerank / vertex.num_outedges();
                for(int i=0; i < vertex.num_outedges(); i++) {
                    graphchi_edge<EdgeDataType,PreEdgeDataType> * edge = vertex.outedge(i);
                    edge->set_data(pagerankcont);
                }
            }
                
            /* Keep track of the progression of the computation.
               GraphChi engine writes a file filename.deltalog. */
            gcontext.log_change(std::abs(pagerank - vertex.get_data()));
            
            /* Set my new pagerank as the vertex value */
            vertex.set_data(pagerank); 

			float new_value = pagerank;
			//compare the new value and the old value
			if( std::abs( new_value - old_value ) > epsilon )
                converged = false;
		}
    }    
    /**
     * Called before an iteration starts.
     */
    void before_iteration(int iteration, graphchi_context &info) {
        iterationcount++;
        converged = iteration > 0;
    }
    
    /**
     * Called after an iteration has finished.
     */
    void after_iteration(int iteration, graphchi_context &ginfo) {
        if (converged) {
            std::cout << "Converged at iteration " << iteration << std::endl;
            ginfo.set_last_iteration(iteration);
        }
		 __sync_synchronize();
    }
    
    /**
     * Called before an execution interval is started.
     */
    void before_exec_interval(vid_t window_st, vid_t window_en, graphchi_context &ginfo) {        
    }
    
    /**
     * Called after an execution interval has finished.
     */
    void after_exec_interval(vid_t window_st, vid_t window_en, graphchi_context &ginfo) {        
    }
    
};

int main(int argc, const char ** argv) {
    /* GraphChi initialization will read the command line 
     arguments and the configuration file. */
    graphchi_init(argc, argv);

    /* Metrics object for keeping track of performance counters
     and other information. Currently required. */
    metrics m("pagerankconverge");
    global_logger().set_log_level(LOG_DEBUG);

    /* Basic arguments for application */
    std::string filename = get_option_string("file");  // Base filename
    int niters           = get_option_int("niters", 1000); // Number of iterations (max)
    scheduler            = get_option_int("scheduler", false);
    epsilon              = get_option_float("epsilon", 0.1);
    int allow_race       = get_option_int("race", false);
    int ntop                = get_option_int("top", 20);
    
	std::cout << "------------------------\tCritical Information\t----------------" << std::endl;
	std::cout << "Epsilon = " << epsilon << std::endl;
	if ( allow_race )
		std::cout << "The Execution allows RACE! (non-deterministic)" << std::endl;
	else
		std::cout << "The Execution is Deterministic!" << std::endl;

	std::cout<< "cpu#=" << get_option_int("execthreads", 3) << std::endl;
	std::cout<< "membudget_mb=" << get_option_int("membudget_mb", 3) << std::endl;
	std::cout<< "epsilon=" << get_option_float("epsilon",0.1) << std::endl;
	std::cout << "------------------------\tCritical Information\t----------------" << std::endl;
	
    /* Process input file - if not already preprocessed */
    int nshards             = (int) convert_if_notexists<PreEdgeDataType>(filename, get_option_string("nshards", "auto"));
    
    if (get_option_int("onlyresult", 0) == 0) {
        /* Run */
        PagerankConverge program;
        graphchi_engine<VertexDataType, EdgeDataType, PreEdgeDataType> engine(filename, nshards, scheduler, m); 
		if( allow_race )
			engine.set_enable_deterministic_parallelism(false);
		else
			engine.set_enable_deterministic_parallelism(true);
			
        engine.run(program, niters);
    }
    
    /* Output top ranked vertices */
    std::vector< vertex_value<float> > top = get_top_vertices<float>(filename, ntop);
    std::cout << "Print top " << ntop << " vertices:" << std::endl;
    for(int i=0; i < (int)top.size(); i++) {
        std::cout << (i+1) << ". " << top[i].vertex << "\t" << top[i].value << std::endl;
    }
    
    metrics_report(m);    
    return 0;
}

