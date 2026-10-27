#include "signal_measure.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void record_measure(const uint8_t *,const SignalNumber *,SignalNumber *,uint32_t,uint32_t,uint32_t,SignalNumber,SignalNumber,SignalNumber);
static void near(double actual,double expected,double tolerance)
{
    if(!(fabs(actual-expected)<=tolerance)){
        fprintf(stderr,"actual %.17g expected %.17g tolerance %.17g\n",actual,expected,tolerance);abort();
    }
}
int main(int argc,char **argv)
{
    const unsigned count=argc>1?1000:10000;unsigned i;
    (void)argv;
    uint8_t *codes=calloc(2000000,1);SignalNumber *values=calloc(1000000,sizeof(SignalNumber)),result[8];
    assert(codes && values);
    for(i=0;i<count;++i){codes[i*2]=85;codes[i*2+1]=195;values[i]=3.25;}
    record_measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1);near(result[0],-.43,1e-6);near(result[1],-.43,1e-6);
    near(result[2],-.43,1e-6);near(result[3],.43,1e-6);near(result[4],0,0);assert(isnan(result[5]));
    for(i=0;i<count;++i){codes[i*2]=i%100<33?195:85;values[i]=(codes[i*2]-128)*.01;}
    record_measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1);near(result[0],-.43,1e-6);near(result[1],.67,1e-6);
    near(result[2],-.067,1e-6);near(result[3],sqrt(.33*.67*.67+.67*.43*.43),1e-6);
    near(result[4],11000,1e-2);near(result[5],33,1e-2);
    record_measure(codes,values,result,count,2,0,.01,128,0);
    near(result[2],-.067,1e-6);assert(result[4]==0 && isnan(result[5]) && result[7]==1);
    for(i=0;i<count;++i)codes[i*2]=42;
    record_measure(codes,values,result,count,0,1,.01,128,1100000);
    near(result[2],-.067,1e-6);near(result[4],11000,1e-2);near(result[5],33,1e-2);
    for(i=0;i<count;++i)codes[i*2]=i%2?195:85;
    record_measure(codes,values,result,count,2,0,.01,128,22000);
    near(result[4],11000,1e-2);near(result[5],50,1e-2);
    uint32_t random=0x12345678;
    for(i=0;i<count;++i){random^=random<<13;random^=random>>17;random^=random<<5;codes[i*2]=(uint8_t)random;}
    record_measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1 && result[6]==1);near(result[4],0,0);assert(isnan(result[5]));
    record_measure(codes,values,result,0,2,0,.01,128,1100000);
    assert(result[7]==1 && isnan(result[0]) && isnan(result[3]) && result[4]==0);
    record_measure(codes,values,result,1000001,2,0,.01,128,1100000);assert(result[7]==0);
    unsigned precise_count=argc>1?1000:1000000;
    for(i=0;i<precise_count;++i)values[i]=i%100<64?1:i%100==64?-0.1724137931034483:-1;
    record_measure(codes,values,result,precise_count,0,1,.01,128,1100000);
    near(result[4],11000,.01);near(result[5],64.35294117647059,.0001);
    free(codes);free(values);
    puts("PASS: DDR measurement replay, DC, 11 kHz/33% duty, raw/filtered, many periods, noise rejection and bounds");
    return 0;
}
