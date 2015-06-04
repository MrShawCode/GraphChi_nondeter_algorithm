#include <stdlib.h>
#include <cmath>
#include <string>
#include <atomic>

#include "graphchi_basic_includes.hpp"
#include "util/toplist.hpp"

using namespace graphchi;

bool        scheduler = true;
vid_t	    single_source = 0;
bool	    reset_edge_value = false;
/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program. 
 */

struct PreEdgeWithSrcValue{
	float value; //value is solid, weight
	float srcValue; // distance , can change

	PreEdgeWithSrcValue()= default;

	PreEdgeWithSrcValue(float v){
		value = v;
	}
	
	PreEdgeWithSrcValue(float v,float sv){
		value = v;
		srcValue = sv;
	}
};

typedef float VertexDataType;       // vid_t is the vertex id type
typedef PreEdgeWithSrcValue PreEdgeDataType;


struct edgeWithSrcValue{
	float value;
	std::atomic<float> srcValue;

	edgeWithSrcValue()= default;

	edgeWithSrcValue(float v){
		value = v;
	}
	
	edgeWithSrcValue(float v,float sv){
		value = v;
		srcValue.store(sv,std::memory_order_relaxed);
	}

	PreEdgeDataType load(std::memory_order sync){
		return PreEdgeDataType(value,srcValue.load(sync));
	}

	void store(PreEdgeDataType edgeWithVlaue, std::memory_order sync){
		srcValue.store(edgeWithVlaue.srcValue,sync);
		value = edgeWithVlaue.value;
	}
};


typedef edgeWithSrcValue EdgeDataType;
typedef float InputEdgeDataType;

/**
 * GraphChi programs need to subclass GraphChiProgram<vertex-type, edge-type> 
 * class. The main logic is usually in the update function.
 */
struct SSSPProgram : public GraphChiProgram<VertexDataType, EdgeDataType, PreEdgeDataType> {
    
     bool converged ;
 
     void set_data( graphchi_vertex<VertexDataType, EdgeDataType, PreEdgeDataType> &vertex, float value ){
 		vertex.set_data(value);
     }

     void update(graphchi_vertex<VertexDataType, EdgeDataType, PreEdgeDataType> &vertex, graphchi_context &gcontext) {
        if (gcontext.iteration == 0) {
		if( vertex.id() == single_source ){
			vertex.set_data( 0.0 );
			converged = false;
			
			for(int j=0;j<vertex.num_outedges();j++){
            	if (scheduler)
					gcontext.scheduler->add_task(vertex.outedge(j)->vertexid, true);
				vertex.outedge(j)->data_ptr->srcValue.store(vertex.get_data(),std::memory_order_relaxed);
			}
		}else{
			vertex.set_data( std::numeric_limits<float>::infinity() );
			for(int j=0;j<vertex.num_outedges();j++){
				vertex.outedge(j)->data_ptr->srcValue.store(vertex.get_data(),std::memory_order_relaxed);
			}
		}

		float edge_value = 0.0;
		if( reset_edge_value ){
			srand( time(0) );
			for( int i=0; i<vertex.num_outedges(); i++){
				graphchi_edge<EdgeDataType,PreEdgeDataType> *edge = vertex.outedge(i);
				edge_value = (float)rand()/(float)RAND_MAX;
				edge->data_ptr->value = edge_value;
				edge->data_ptr->srcValue.store(0,std::memory_order_relaxed);
				if(vertex.id()==0) printf( "value of out edge %d is %f\n", i, edge_value );
			}
		}
        }
     

	if( gcontext.iteration > 0 ){
		for( int i=0; i<vertex.num_inedges(); i++ ){
			float inedge_value = vertex.inedge(i)->data_ptr->srcValue.load(std::memory_order_relaxed);
			if( ( inedge_value + vertex.inedge(i)->get_data().value ) < vertex.get_data() ){
				converged = false;
				set_data( vertex,  inedge_value + vertex.inedge(i)->get_data().value );
				for(int j=0;j<vertex.num_outedges();j++){
            				if (scheduler)
						gcontext.scheduler->add_task(vertex.outedge(j)->vertexid, true);
					vertex.outedge(j)->data_ptr->srcValue.store(vertex.get_data(),std::memory_order_relaxed);
				}  
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
    metrics m("sssp");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file");  // Base filename
    int niters           = get_option_int("niters", 1000); // Number of iterations (max)
    scheduler            = get_option_int("scheduler", true);
    single_source		 = get_option_int("single_source", 0);
    reset_edge_value	 = get_option_int("reset_edge_value", false);
    int allow_race		 = get_option_int("race", false);
    int ntop 			 = get_option_int("top",100);
    
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
    int nshards             = (int) convert_if_notexists<InputEdgeDataType,PreEdgeDataType>(filename, get_option_string("nshards", "auto"));
    
    /* Run */
    SSSPProgram program;
    graphchi_engine<VertexDataType, EdgeDataType, PreEdgeDataType> engine(filename, nshards, scheduler, m); 
	if( allow_race )
		engine.set_enable_deterministic_parallelism(false);
	else
		engine.set_enable_deterministic_parallelism(true);
    engine.run(program, niters);
    
    /* Report execution metrics */
    std::vector<vertex_value<float> > top = get_top_vertices<float>(filename,ntop);
    std::cout<<"Print head "<<ntop<<" vertices: "<< std::endl;
    for(int i=0;i<(int)top.size();i++)
	std::cout<<top[i].vertex<<"\t"<<top[i].value<<std::endl;

    metrics_report(m);
    return 0;
}

