#ifndef SIGNAL_EDGES_H
#define SIGNAL_EDGES_H
#include "signal_number.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    SignalNumber level,band,previous,numerator,denominator;
    size_t crossing_sample;
    int side;
} SignalEdgeDetector;

static inline void signal_edge_init(SignalEdgeDetector *e,SignalNumber level,SignalNumber band,SignalNumber value)
{
    *e=(SignalEdgeDetector){level,band,value,0,1,SIZE_MAX,value<=level-band?-1:value>=level+band?1:0};
}
static inline SignalNumber signal_edge_time(const SignalEdgeDetector *e)
{
    return e->crossing_sample-1+e->numerator/e->denominator;
}
/* Confirmation and timestamp are separate: ringing inside the guard cannot
   create another edge, and interpolation keeps the original level crossing. */
static inline int signal_edge_advance(SignalEdgeDetector *e,SignalNumber value,size_t sample)
{
    int direction=0;
    if(!e->band){
        direction=e->previous<=e->level && value>e->level?1:
            e->previous>=e->level && value<e->level?-1:0;
        if(direction){
            e->crossing_sample=sample;e->numerator=e->level-e->previous;e->denominator=value-e->previous;
        }
    }else{
        if(e->crossing_sample==SIZE_MAX &&
           ((e->side<0 && e->previous<=e->level && value>e->level) ||
            (e->side>0 && e->previous>=e->level && value<e->level))){
            e->crossing_sample=sample;
            e->numerator=e->level-e->previous;e->denominator=value-e->previous;
        }
        int side=value<=e->level-e->band?-1:value>=e->level+e->band?1:0;
        if(side){
            if(e->side && side!=e->side && e->crossing_sample!=SIZE_MAX)direction=side;
            e->side=side;
            if(!direction)e->crossing_sample=SIZE_MAX;
        }
    }
    e->previous=value;
    return direction;
}
#endif
