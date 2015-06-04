#include <cmath>
#include <string>
#include <pthread.h>

#include "graphchi_basic_includes.hpp"
#include "util/labelanalysis.hpp"

using namespace graphchi;

int         iterationcount = 0;
bool        scheduler = false;
bool        enable_mutex = true;

/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program. 
 */
typedef vid_t VertexDataType;       // vid_t is the vertex id type
struct edge_with_lock{
	vid_t value;
	pthread_mutex_t mutex;

	edge_with_lock(){}

	edge_with_lock(float){}
};
typedef edge_with_lock EdgeDataType;

/**
 * GraphChi programs need to subclass GraphChiProgram<vertex-type, edge-type> 
 * class. The main logic is usually in the update function.
 */
struct ConnectedComponentsProgram : public GraphChiProgram<VertexDataType, EdgeDataType> {
    
    bool converged;
    
    void update(graphchi_vertex<VertexDataType, EdgeDataType> &vertex, graphchi_context &gcontext) {
        
        if (scheduler) gcontext.scheduler->remove_tasks(vertex.id(), vertex.id());
        
        if (gcontext.iteration == 0) {
            vertex.set_data(vertex.id());
            if (scheduler)  gcontext.scheduler->add_task(vertex.id());
        }
        
        /* On subsequent iterations, find the minimum label of my neighbors */
        vid_t curmin = vertex.get_data();
        for(int i=0; i < vertex.num_edges(); i++) {
			vid_t nblabel = 0;
			if(enable_mutex)
				pthread_mutex_lock( &vertex.edge(i)->data_ptr->mutex );
            nblabel = vertex.edge(i)->data_ptr->value;
			if(enable_mutex)
				pthread_mutex_unlock( &vertex.edge(i)->data_ptr->mutex );
            if (gcontext.iteration == 0) nblabel = vertex.edge(i)->vertex_id();  // Note!
            curmin = std::min(nblabel, curmin); 
        }
        
        /* Set my label */
        vertex.set_data(curmin);
        
        /** 
         * Broadcast new label to neighbors by writing the value
         * to the incident edges.
         * Note: on first iteration, write only to out-edges to avoid
         * overwriting data (this is kind of a subtle point)
         */
        vid_t label = vertex.get_data();
        
        if (gcontext.iteration > 0) {
            for(int i=0; i < vertex.num_edges(); i++) {
				vid_t edge_value = 0;
				if(enable_mutex)
					pthread_mutex_lock( &vertex.edge(i)->data_ptr->mutex );
				edge_value = vertex.edge(i)->data_ptr->value;
				if(enable_mutex)
					pthread_mutex_unlock( &vertex.edge(i)->data_ptr->mutex );
                if (label < edge_value) {
					if(enable_mutex)
						pthread_mutex_lock( &vertex.edge(i)->data_ptr->mutex );
					vertex.edge(i)->data_ptr->value = label;
					if(enable_mutex)
						pthread_mutex_unlock( &vertex.edge(i)->data_ptr->mutex );
                    if (scheduler) gcontext.scheduler->add_task(vertex.edge(i)->vertex_id(), true);
                    converged = false;
                }
            }
        } else if (gcontext.iteration == 0) {
            for(int i=0; i < vertex.num_outedges(); i++) {
                vertex.outedge(i)->data_ptr->value  = label;
				pthread_mutex_init(&vertex.outedge(i)->data_ptr->mutex,NULL);
            }
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
    metrics m("connected-components");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file");  // Base filename
    int niters           = get_option_int("niters", 1000); // Number of iterations (max)
    scheduler            = get_option_int("scheduler", false);
	int allow_race		 = get_option_int("race", false);
	enable_mutex 		 = get_option_int("lock",true);
    
	std::cout << "------------------------\tCritical Information\t----------------" << std::endl;
	if ( allow_race )
		std::cout << "The Execution allows RACE! (non-deterministic)" << std::endl;
	else
		std::cout << "The Execution is Deterministic!" << std::endl;

	std::cout<< "cpu#=" << get_option_int("execthreads", 3) << std::endl;
    std::cout<< "membudget_mb=" << get_option_int("membudget_mb", 3) << std::endl;
	std::cout << "------------------------\tCritical Information\t----------------" << std::endl;

    /* Process input file - if not already preprocessed */
    int nshards             = (int) convert_if_notexists<float,EdgeDataType>(filename, get_option_string("nshards", "auto"));
    
    if (get_option_int("onlyresult", 0) == 0) {
        /* Run */
        ConnectedComponentsProgram program;
        graphchi_engine<VertexDataType, EdgeDataType> engine(filename, nshards, scheduler, m); 
		if( allow_race )
			engine.set_enable_deterministic_parallelism(false);
		else
			engine.set_enable_deterministic_parallelism(true);
        engine.run(program, niters);
    }
    
    /* Run analysis of the connected components  (output is written to a file) */
    m.start_time("label-analysis");
    
    analyze_labels<vid_t>(filename);
    
    m.stop_time("label-analysis");
    
    /* Report execution metrics */
    metrics_report(m);
    return 0;
}

