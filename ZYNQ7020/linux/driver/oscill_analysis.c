// SPDX-License-Identifier: GPL-2.0
/* Synchronous DDR replay for the numerical FPGA kernel. */
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/oscill_analysis.h>
#include "oscill_capture_internal.h"

#define ARENA_BYTES SZ_8M
#define VALUES_OFFSET SZ_2M
#define RESULT_OFFSET (SZ_2M+SZ_4M)
#define CONTROL 0x00
#define GLOBAL_IRQ 0x04
#define IRQ_ENABLE 0x08
#define IRQ_STATUS 0x0c
#define INPUT_CODES 0x10
#define INPUT_VALUES 0x18
#define OUTPUT_VALUES 0x20
#define INPUT_COUNT 0x28
#define INPUT_STRIDE 0x30
#define INPUT_FORMAT 0x38
#define INPUT_GAIN 0x40
#define INPUT_ZERO 0x48
#define INPUT_RATE 0x50

struct analysis {
    struct miscdevice misc;
    struct device *capture;
    void __iomem *registers;
    void *memory;
    dma_addr_t dma;
    atomic_t busy;
    struct mutex lock;
    struct completion done;
    bool failed;
};
static bool present(struct analysis *a)
{
    const u32 required=OSC_CAPTURE_ANALYSIS_PRESENT|OSC_CAPTURE_ANALYSIS_AMPLITUDE_ONLY;
    return (oscill_capture_status(a->capture)&required)==required;
}
static irqreturn_t analysis_interrupt(int irq,void *argument)
{
    struct analysis *a=argument;
    u32 pending;
    if(!present(a))return IRQ_NONE;
    pending=readl(a->registers+IRQ_STATUS);
    if(!(pending&1))return IRQ_NONE;
    writel(pending,a->registers+IRQ_STATUS);
    complete(&a->done);
    return IRQ_HANDLED;
}
static int analysis_open(struct inode *inode,struct file *file)
{
    struct analysis *a=container_of(file->private_data,struct analysis,misc);
    int error=0;
    if(!present(a))return -ENODEV;
    if(atomic_cmpxchg(&a->busy,0,1))return -EBUSY;
    mutex_lock(&a->lock);
    if(a->failed)error=-EIO;
    else if(!a->memory){
        /* The arena belongs to the fixed device, including after a timeout.
           Closing an fd must never free memory an AXI master may still use. */
        a->memory=dmam_alloc_coherent(a->misc.parent,ARENA_BYTES,&a->dma,GFP_KERNEL);
        if(!a->memory)error=-ENOMEM;
    }
    mutex_unlock(&a->lock);
    if(error){atomic_set(&a->busy,0);return error;}
    file->private_data=a;
    return 0;
}
static int analysis_release(struct inode *inode,struct file *file)
{
    struct analysis *a=file->private_data;
    atomic_set(&a->busy,0);
    return 0;
}
static bool finite_bits(u32 value)
{
    return (value&0x7f800000)!=0x7f800000;
}
static int measure(struct analysis *a,struct osc_analysis_measure *job)
{
    __le32 *result;
    size_t bytes;
    u32 pending;
    unsigned i;
    if(job->samples>OSC_ANALYSIS_MAX_SAMPLES || job->format>1 || job->stride>2 ||
       (!job->format && !job->stride) || job->reserved[0] || job->reserved[1] ||
       !finite_bits(job->gain_bits) || !finite_bits(job->zero_bits) ||
       !finite_bits(job->sample_rate_bits) || (job->sample_rate_bits&0x80000000) ||
       job->codes_address>ULONG_MAX || job->values_address>ULONG_MAX)return -EINVAL;
    if(a->failed)return -EIO;
    if(!present(a))return -ENODEV;
    if(!(readl(a->registers+CONTROL)&4))return -EBUSY;
    if(job->samples){
        if(job->format){
            bytes=(size_t)job->samples*sizeof(u32);
            if(copy_from_user(a->memory+VALUES_OFFSET,u64_to_user_ptr(job->values_address),bytes))return -EFAULT;
        }else{
            bytes=(size_t)(job->samples-1)*job->stride+1;
            if(copy_from_user(a->memory,u64_to_user_ptr(job->codes_address),bytes))return -EFAULT;
        }
    }
    result=a->memory+RESULT_OFFSET;
    memset(result,0,sizeof(job->result_bits));
    writel(lower_32_bits(a->dma),a->registers+INPUT_CODES);
    writel(lower_32_bits(a->dma+VALUES_OFFSET),a->registers+INPUT_VALUES);
    writel(lower_32_bits(a->dma+RESULT_OFFSET),a->registers+OUTPUT_VALUES);
    writel(job->samples,a->registers+INPUT_COUNT);
    writel(job->stride,a->registers+INPUT_STRIDE);
    writel(job->format,a->registers+INPUT_FORMAT);
    writel(job->gain_bits,a->registers+INPUT_GAIN);
    writel(job->zero_bits,a->registers+INPUT_ZERO);
    writel(job->sample_rate_bits,a->registers+INPUT_RATE);
    pending=readl(a->registers+IRQ_STATUS);
    if(pending)writel(pending,a->registers+IRQ_STATUS);
    reinit_completion(&a->done);
    writel(1,a->registers+IRQ_ENABLE);writel(1,a->registers+GLOBAL_IRQ);
    dma_wmb();
    writel(1,a->registers+CONTROL);
    if(!wait_for_completion_timeout(&a->done,msecs_to_jiffies(5000))){
        a->failed=true;
        dev_err(a->misc.parent,"analysis: FPGA replay timed out\n");
        return -ETIMEDOUT;
    }
    dma_rmb();
    for(i=0;i<ARRAY_SIZE(job->result_bits);++i)job->result_bits[i]=le32_to_cpup(result+i);
    return job->result_bits[7]==0x3f800000?0:-EIO;
}
static long analysis_ioctl(struct file *file,unsigned int command,unsigned long argument)
{
    struct analysis *a=file->private_data;
    void __user *user=(void __user *)argument;
    struct osc_analysis_measure job;
    int error;
    if(command==OSC_ANALYSIS_GET_INFO){
        struct osc_analysis_info info={OSC_ANALYSIS_ABI,OSC_ANALYSIS_MAX_SAMPLES,
            OSC_ANALYSIS_BYTES|OSC_ANALYSIS_VALUES|OSC_ANALYSIS_AMPLITUDE_ONLY,0};
        return copy_to_user(user,&info,sizeof(info))?-EFAULT:0;
    }
    if(command!=OSC_ANALYSIS_MEASURE)return -ENOTTY;
    if(copy_from_user(&job,user,sizeof(job)))return -EFAULT;
    if(mutex_lock_interruptible(&a->lock))return -ERESTARTSYS;
    error=measure(a,&job);
    mutex_unlock(&a->lock);
    if(!error && copy_to_user(user,&job,sizeof(job)))error=-EFAULT;
    return error;
}
static const struct file_operations analysis_ops={
    .owner=THIS_MODULE,.open=analysis_open,.release=analysis_release,
    .unlocked_ioctl=analysis_ioctl,.compat_ioctl=analysis_ioctl,.llseek=no_llseek
};
static void put_capture(void *argument)
{
    put_device(argument);
}
static int analysis_probe(struct platform_device *device)
{
    struct analysis *a;
    struct device_node *node;
    struct platform_device *capture;
    struct clk *clock;
    int irq,error;
    a=devm_kzalloc(&device->dev,sizeof(*a),GFP_KERNEL);
    if(!a)return -ENOMEM;
    node=of_parse_phandle(device->dev.of_node,"oscill,capture",0);
    if(!node)return -EINVAL;
    capture=of_find_device_by_node(node);of_node_put(node);
    if(!capture)return -EPROBE_DEFER;
    a->capture=&capture->dev;
    error=devm_add_action_or_reset(&device->dev,put_capture,a->capture);
    if(error)return error;
    if(!oscill_capture_status(a->capture))return -EPROBE_DEFER;
    a->registers=devm_platform_ioremap_resource(device,0);
    if(IS_ERR(a->registers))return PTR_ERR(a->registers);
    clock=devm_clk_get_enabled(&device->dev,"ap_clk");
    if(IS_ERR(clock))return dev_err_probe(&device->dev,PTR_ERR(clock),"analysis clock\n");
    error=dma_set_mask_and_coherent(&device->dev,DMA_BIT_MASK(32));
    if(error)return error;
    irq=platform_get_irq(device,0);if(irq<0)return irq;
    init_completion(&a->done);mutex_init(&a->lock);atomic_set(&a->busy,0);
    error=devm_request_irq(&device->dev,irq,analysis_interrupt,0,dev_name(&device->dev),a);
    if(error)return error;
    a->misc=(struct miscdevice){.minor=MISC_DYNAMIC_MINOR,.name="oscill-analysis",
        .fops=&analysis_ops,.parent=&device->dev,.mode=0600};
    return misc_register(&a->misc);
}
static const struct of_device_id analysis_match[]={{.compatible="oscill,record-analysis"},{}};
static struct platform_driver analysis_driver={
    .probe=analysis_probe,
    .driver={.name="oscill-analysis",.of_match_table=analysis_match,.suppress_bind_attrs=true}
};
builtin_platform_driver(analysis_driver);
MODULE_LICENSE("GPL");
