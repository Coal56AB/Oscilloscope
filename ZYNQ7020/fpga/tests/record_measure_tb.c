#include "signal_measure.h"
#include "signal_filter.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void record_measure(const uint8_t *,const SignalNumber *,SignalNumber *,uint32_t,uint32_t,uint32_t,SignalNumber,SignalNumber,SignalNumber,uint32_t,uint32_t,SignalNumber);
static void measure(const uint8_t *codes,const SignalNumber *values,SignalNumber *result,
    uint32_t count,uint32_t stride,uint32_t format,SignalNumber gain,SignalNumber zero,SignalNumber rate)
{
    record_measure(codes,values,result,count,stride,format,gain,zero,rate,0,0,0);
}
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
    uint8_t *codes=calloc(2000000,1);SignalNumber *values=calloc(1000000,sizeof(SignalNumber));
    SignalNumber *result=calloc(1000008,sizeof(*result));assert(codes && values && result);
    for(i=0;i<count;++i){codes[i*2]=85;codes[i*2+1]=195;values[i]=3.25;}
    measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1);near(result[0],-.43,1e-6);near(result[1],-.43,1e-6);
    near(result[2],-.43,1e-6);near(result[3],.43,1e-6);near(result[4],0,0);assert(isnan(result[5]));
    for(i=0;i<count;++i){codes[i*2]=i%100<33?195:85;values[i]=(codes[i*2]-128)*.01;}
    measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1);near(result[0],-.43,1e-6);near(result[1],.67,1e-6);
    near(result[2],-.067,1e-6);near(result[3],sqrt(.33*.67*.67+.67*.43*.43),1e-6);
    near(result[4],11000,1e-2);near(result[5],33,1e-2);
    measure(codes,values,result,count,2,0,.01,128,0);
    near(result[2],-.067,1e-6);assert(result[4]==0 && isnan(result[5]) && result[7]==1);
    for(i=0;i<count;++i)codes[i*2]=42;
    measure(codes,values,result,count,0,1,.01,128,1100000);
    near(result[2],-.067,1e-6);near(result[4],11000,1e-2);near(result[5],33,1e-2);
    for(i=0;i<count;++i)codes[i*2]=i%2?195:85;
    measure(codes,values,result,count,2,0,.01,128,22000);
    near(result[4],11000,1e-2);near(result[5],50,1e-2);
    uint32_t random=0x12345678;
    for(i=0;i<count;++i){random^=random<<13;random^=random>>17;random^=random<<5;codes[i*2]=(uint8_t)random;}
    measure(codes,values,result,count,2,0,.01,128,1100000);
    assert(result[7]==1 && result[6]==1);near(result[4],0,0);assert(isnan(result[5]));
    measure(codes,values,result,0,2,0,.01,128,1100000);
    assert(result[7]==1 && isnan(result[0]) && isnan(result[3]) && result[4]==0);
    measure(codes,values,result,1000001,2,0,.01,128,1100000);assert(result[7]==0);
    unsigned precise_count=argc>1?1000:1000000;
    for(i=0;i<precise_count;++i)values[i]=i%100<64?1:i%100==64?-0.1724137931034483:-1;
    measure(codes,values,result,precise_count,0,1,.01,128,1100000);
    near(result[4],11000,.01);near(result[5],64.35294117647059,.0001);
    SignalNumber *filtered=calloc(1000008,sizeof(*filtered)),*expected=calloc(count,sizeof(*expected));
    assert(filtered && expected);
    for(i=0;i<count;++i){codes[2*i]=i%101<33?190+(i%7):80+(i%5);values[i]=(codes[2*i]-128)*.01f;}
    for(unsigned format=0;format<2;++format)for(unsigned level=0;level<4;++level){
        SignalTrace trace={codes,values,count,2,.01f,128,format};
        signal_filter_trace(&trace,.025f,level,expected);
        record_measure(codes,values,filtered,count,2,format,.01f,128,0,1,level,.025f);
        assert(filtered[7]==1);
        for(i=0;i<count;++i)near(filtered[8+i],expected[i],.000002);
    }
    free(filtered);free(expected);
    free(codes);free(values);free(result);
    puts("PASS: DDR measurement replay, DC, 11 kHz/33% duty, raw/filtered, many periods, noise rejection and bounds");
    return 0;
}
