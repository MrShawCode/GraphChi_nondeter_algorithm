#include <stdlib.h>
#include <cmath>
#include <string>
#include <pthread.h>

#include "graphchi_basic_includes.hpp"
#include "util/vertexlist.hpp"
#define HL_MAX 100000

using namespace graphchi;

bool        scheduler = true;
vid_t	    single_source = 0;
bool	    reset_edge_value = false;
bool 		enable_mutex = true;
/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program. 
 */
struct edge_with_lock{
	int value;
	pthread_mutex_t mutex;	
	
	edge_with_lock(){}

	edge_with_lock(float v){

	}
};
typedef int VertexDataType;       // vid_t is the vertex id type
typedef edge_with_lock EdgeDataType;
typedef float InputEdgeDataType;


/**
 * GraphChi programs need to subclass GraphChiProgram<vertex-type, edge-type> 
 * class. The main logic is usually in the update function.
 */
struct BFSProgram : public GraphChiProgram<VertexDataType, EdgeDataType> {
    
    bool converged ;

    void update(graphchi_vertex<VertexDataType, EdgeDataType> &vertex, graphchi_context &gcontext) {
        if (gcontext.iteration == 0) {
			if( vertex.id() == single_source ){
				vertex.set_data(0);
				//converged = false;
			
				int vertex_data = vertex.get_data();
				for(int j=0;j<vertex.num_outedges();j++){
            		if (scheduler)
						gcontext.scheduler->add_task(vertex.outedge(j)->vertexid, true);
					vertex.outedge(j)->data_ptr->value = vertex_data;
					pthread_mutex_init(&vertex.outedge(j)->data_ptr->mutex,NULL);
				}
			}else{
				vertex.set_data(HL_MAX);
				int vertex_data = vertex.get_data();
				for(int j=0;j<vertex.num_outedges();j++){
					vertex.outedge(j)->data_ptr->value = vertex_data;
					pthread_mutex_init(&vertex.outedge(j)->data_ptr->mutex,NULL);
				}
			}
   		}else if( gcontext.iteration > 0 ){
			for( int i=0; i<vertex.num_inedges(); i++ ){
				int inedge_value = HL_MAX;
				if(enable_mutex)
						pthread_mutex_lock( &vertex.inedge(i)->data_ptr->mutex);
				inedge_value = vertex.inedge(i)->data_ptr->value;
				if(enable_mutex)
						pthread_mutex_unlock( &vertex.inedge(i)->data_ptr->mutex);
				if( inedge_value+1 < vertex.get_data() ){
					converged = false;
					vertex.set_data(inedge_value+1);
				}
			}
			if(!converged){
				int vertex_data = vertex.get_data();
				for(int j=0;j<vertex.num_outedges();j++){
            		if (scheduler)
							gcontext.scheduler->add_task(vertex.outedge(j)->vertexid, true);
					if(enable_mutex)
						pthread_mutex_lock( &vertex.outedge(j)->data_ptr->mutex);
					vertex.outedge(j)->data_ptr->value = vertex_data;
					if(enable_mutex)
						pthread_mutex_unlock( &vertex.outedge(j)->data_ptr->mutex);
				}
			}	
		}
    }

    /**
     * Called before an iteration starts.
     */
    void before_iteration(int iteration, graphchi_context &info) {
		if( iteration == 0 ){
			std::cout<< "The system will run edge-consistency  mode -- " << std::endl;
		}
        
		converged = iteration > 0;
    }
    
    /**
     * Called after an iteration has finished.
     */
    void after_iteration(int iteration, graphchi_context &ginfo) {
        if (converged) {
            std::cout << "Converged!" << std::endl;
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
    metrics m("bfs");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file");  // Base filename
    int niters           = get_option_int("niters", 1000); // Number of iterations (max)
    scheduler            = get_option_int("scheduler", true);
    single_source		 = get_option_int("single_source", 0);
    reset_edge_value	 = get_option_int("reset_edge_value", false);
    int allow_race		 = get_option_int("race", false);
    int ntop 			 = get_option_int("top",100);
	enable_mutex		= get_option_int("mutex",true);
    
    std::cout << "------------------------\tExecution Mode\t----------------" << std::endl;
    if ( allow_race )
	std::cout << "--The Execution allows RACE! (non-deterministic)" << std::endl;
    else
	std::cout << "--The Execution is Deterministic!" << std::endl;
    if ( scheduler )
	std::cout << "--The Execution uses scheduler. " << std::endl;
    else
	std::cout << "--The Execution uses no scheduler. " << std::endl;

    std::cout << "--Single source = " << single_source << std::endl;
    if( reset_edge_value )
	std::cout << "--Reset edge value = " << "TRUE" << std::endl;
    else
	std::cout << "--Reset edge value = " << "FALSE" << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    /* Process input file - if not already preprocessed */
    int nshards             = (int) convert_if_notexists<InputEdgeDataType,EdgeDataType>(filename, get_option_string("nshards", "auto"));
    
    /* Run */
    BFSProgram program;
    graphchi_engine<VertexDataType, EdgeDataType> engine(filename, nshards, scheduler, m); 
	if( allow_race )
		engine.set_enable_deterministic_parallelism(false);
	else
		engine.set_enable_deterministic_parallelism(true);
    engine.run(program, niters);
    
    /* Report execution metrics */
    std::vector<vertex_value<int> > top = get_header_vertices<int>(filename,ntop);
    std::cout<<"Print head "<<ntop<<" vertices: "<< std::endl;
    for(int i=0;i<(int)top.size();i++)
	std::cout<<top[i].vertex<<"\t"<<top[i].value<<std::endl;

    metrics_report(m);
    return 0;
}

