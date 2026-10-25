#include "dsp_fft.h"
#include <math.h>
void dsp_fft(double *re,double *im,size_t n,int inverse)
{
    size_t i,j,length;
    for(i=1,j=0;i<n;++i){
        size_t bit=n>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j){double t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t;}
    }
    for(length=2;length<=n;length<<=1){
        double angle=(inverse?1:-1)*6.283185307179586/length;
        double step_r=cos(angle),step_i=sin(angle);
        size_t half=length/2;
        for(i=0;i<n;i+=length){
            double wr=1,wi=0;
            for(j=0;j<half;++j){
                double tr=wr*re[i+j+half]-wi*im[i+j+half];
                double ti=wr*im[i+j+half]+wi*re[i+j+half];
                double next=wr*step_r-wi*step_i;
                re[i+j+half]=re[i+j]-tr;im[i+j+half]=im[i+j]-ti;
                re[i+j]+=tr;im[i+j]+=ti;
                wi=wr*step_i+wi*step_r;wr=next;
            }
        }
    }
    if(inverse)for(i=0;i<n;++i){re[i]/=n;im[i]/=n;}
}
