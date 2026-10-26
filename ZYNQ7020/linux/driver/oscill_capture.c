// SPDX-License-Identifier: GPL-2.0
/* AD9288 AXI DMA SG receiver. Coherent memory is leased read-only to userspace. */
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/oscill_capture.h>

#define BUFFERS 16
#define BLOCK_BYTES SZ_2M
#define HEADER_BYTES 64
#define PAYLOAD_BYTES (BLOCK_BYTES - HEADER_BYTES)
#define MAPPING_BYTES (BUFFERS * BLOCK_BYTES)
#define GPIO_STATUS 8
#define PL_ID 0xadc50000

struct receiver {
    struct miscdevice misc;
    struct dma_chan *chan;
    void __iomem *gpio;
    atomic_t busy;
};
enum buffer_state { QUEUED, READY, LEASED };
struct session;
struct capture_slot {
    struct session *session;
    unsigned index;
    enum buffer_state state;
    u64 sequence, first_sample;
};
struct session {
    struct receiver *receiver;
    struct kref ref;
    struct mutex ioctl_lock;
    spinlock_t lock;
    wait_queue_head_t wait;
    struct delayed_work monitor;
    void *memory;
    dma_addr_t dma;
    struct capture_slot slot[BUFFERS];
    unsigned inflight;
    u32 payload_bytes, sample_rate, control, probe_control;
    bool peak_detect;
    bool stopped, error;
    u64 next_sequence;
    unsigned long last_completion;
};

static void session_free(struct kref *ref)
{
    struct session *s = container_of(ref, struct session, ref);
    dma_free_coherent(s->receiver->chan->device->dev, MAPPING_BYTES, s->memory, s->dma);
    atomic_set(&s->receiver->busy, 0);
    kfree(s);
}

static int submit_slot(struct capture_slot *slot);

/* Session lock held; retain the first failure and log its cause once. */
static void capture_error(struct session *s, const char *reason)
{
    if (!s->error) dev_err(s->receiver->misc.parent, "capture: %s\n", reason);
    s->error = true;
}

static void completed(void *argument, const struct dmaengine_result *result)
{
    struct capture_slot *slot = argument;
    struct session *s = slot->session;
    __le32 *header = s->memory + slot->index * BLOCK_BYTES;
    unsigned long flags;
    unsigned i;
    struct capture_slot *oldest = NULL;

    /* Coherent allocation: no streaming sync; order payload reads after DMA. */
    dma_rmb();
    spin_lock_irqsave(&s->lock, flags);
    if (s->stopped || s->error) goto out;
    if (result && (result->result != DMA_TRANS_NOERROR || result->residue)) {
        capture_error(s, "DMA completion error or incomplete transfer");
        goto out;
    }
    if (le32_to_cpu(header[0]) != 0x5043534f || le32_to_cpu(header[1]) != 4 ||
        le32_to_cpu(header[2]) != s->payload_bytes || le32_to_cpu(header[3]) != s->sample_rate ||
        le32_to_cpu(header[8]) != (s->peak_detect ? OSC_CAPTURE_PEAKS : 0)) {
        capture_error(s, "invalid DMA block header");
        goto out;
    }
    slot->sequence = (u64)le32_to_cpu(header[4]) | (u64)le32_to_cpu(header[5]) << 32;
    slot->first_sample = (u64)le32_to_cpu(header[6]) | (u64)le32_to_cpu(header[7]) << 32;
    slot->state = READY;
    s->inflight--;
    s->last_completion = jiffies;
    /* Recycle only unleased READY blocks. Keep half the ring queued at DMA. */
    if (s->inflight < BUFFERS / 2) {
        for (i = 0; i < BUFFERS; i++)
            if (s->slot[i].state == READY &&
                (!oldest || s->slot[i].sequence < oldest->sequence)) oldest = &s->slot[i];
        if (!oldest) capture_error(s, "no unleased buffer for DMA");
        else if (submit_slot(oldest)) capture_error(s, "cannot queue DMA buffer");
    }
out:
    spin_unlock_irqrestore(&s->lock, flags);
    dma_async_issue_pending(s->receiver->chan);
    wake_up_interruptible(&s->wait);
}

/* Called with session lock held once acquisition is running. */
static int submit_slot(struct capture_slot *slot)
{
    struct session *s = slot->session;
    struct dma_async_tx_descriptor *tx;
    struct scatterlist sg;
    dma_cookie_t cookie;
    sg_init_table(&sg, 1);
    sg_dma_address(&sg) = s->dma + slot->index * BLOCK_BYTES;
    sg_dma_len(&sg) = HEADER_BYTES + s->payload_bytes;
    tx = dmaengine_prep_slave_sg(s->receiver->chan, &sg, 1, DMA_DEV_TO_MEM,
                                DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
    if (!tx) return -EIO;
    tx->callback_result = completed;
    tx->callback_param = slot;
    cookie = dmaengine_submit(tx);
    if (dma_submit_error(cookie)) return -EIO;
    slot->state = QUEUED;
    s->inflight++;
    return 0;
}

static void monitor(struct work_struct *work)
{
    struct session *s = container_of(to_delayed_work(work), struct session, monitor);
    unsigned long flags;
    spin_lock_irqsave(&s->lock, flags);
    if (!s->stopped && !s->error) {
        if (readl(s->receiver->gpio + GPIO_STATUS) & 1)
            capture_error(s, "input FIFO overflow");
        else if (time_after(jiffies, s->last_completion + HZ))
            capture_error(s, "no completed DMA block for 1 second");
    }
    if (!s->stopped) schedule_delayed_work(&s->monitor, HZ / 10);
    spin_unlock_irqrestore(&s->lock, flags);
    wake_up_interruptible(&s->wait);
}

static void stop_session(struct session *s)
{
    unsigned long flags;
    spin_lock_irqsave(&s->lock, flags);
    s->stopped = true;
    spin_unlock_irqrestore(&s->lock, flags);
    cancel_delayed_work_sync(&s->monitor);
    dmaengine_terminate_sync(s->receiver->chan);
    writel(s->control, s->receiver->gpio);
    /* terminate resets AXI DMA while the old stream can still be valid.
       S2MM accepts a few beats even with RS=0. Flush those prefetched beats
       after the input/CDC reset has propagated, before queuing a new epoch. */
    udelay(10);
    dmaengine_terminate_sync(s->receiver->chan);
}

static int capture_open(struct inode *inode, struct file *file)
{
    struct receiver *r = container_of(file->private_data, struct receiver, misc);
    struct session *s;
    struct dma_slave_config config = { .direction = DMA_DEV_TO_MEM };
    unsigned i;
    int error;
    if (atomic_cmpxchg(&r->busy, 0, 1)) return -EBUSY;
    s = kzalloc(sizeof(*s), GFP_KERNEL);
    if (!s) { atomic_set(&r->busy, 0); return -ENOMEM; }
    s->receiver = r;
    s->payload_bytes = 8u << 17; s->sample_rate = 200000000 >> 6;
    s->control = (17u << 6) | (6u << 11);
    kref_init(&s->ref);
    mutex_init(&s->ioctl_lock);
    spin_lock_init(&s->lock);
    init_waitqueue_head(&s->wait);
    INIT_DELAYED_WORK(&s->monitor, monitor);
    s->memory = dma_alloc_coherent(r->chan->device->dev, MAPPING_BYTES, &s->dma, GFP_KERNEL);
    if (!s->memory) { kfree(s); atomic_set(&r->busy, 0); return -ENOMEM; }
    writel(s->control, r->gpio);
    error = dmaengine_slave_config(r->chan, &config);
    if (error) goto fail;
    for (i = 0; i < BUFFERS; i++) {
        s->slot[i].session = s;
        s->slot[i].index = i;
        error = submit_slot(&s->slot[i]);
        if (error) goto fail;
    }
    s->last_completion = jiffies;
    dma_async_issue_pending(r->chan);
    writel(s->control | 1, r->gpio);
    schedule_delayed_work(&s->monitor, HZ / 10);
    file->private_data = s;
    return nonseekable_open(inode, file);
fail:
    stop_session(s);
    kref_put(&s->ref, session_free);
    return error;
}

static int capture_release(struct inode *inode, struct file *file)
{
    struct session *s = file->private_data;
    stop_session(s);
    kref_put(&s->ref, session_free);
    return 0;
}
static int capture_flush(struct file *file, fl_owner_t owner)
{
    struct session *s = file->private_data;
    /* mmap holds a file reference: stop on fd close, before final .release. */
    mutex_lock(&s->ioctl_lock);
    stop_session(s);
    mutex_unlock(&s->ioctl_lock);
    return 0;
}

static __poll_t capture_poll(struct file *file, poll_table *wait)
{
    struct session *s = file->private_data;
    __poll_t result = 0;
    unsigned long flags;
    unsigned i;
    poll_wait(file, &s->wait, wait);
    spin_lock_irqsave(&s->lock, flags);
    if (s->stopped) result = EPOLLHUP;
    else if (s->error) result = EPOLLERR;
    else for (i = 0; i < BUFFERS; i++)
        if (s->slot[i].state == READY) { result = EPOLLIN | EPOLLRDNORM; break; }
    spin_unlock_irqrestore(&s->lock, flags);
    return result;
}

static long capture_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    struct session *s = file->private_data;
    void __user *user = (void __user *)argument;
    struct osc_capture_info info = {
        .abi_version = OSC_CAPTURE_ABI, .register_version = OSC_CAPTURE_REGISTER_VERSION,
        .sample_format = OSC_CAPTURE_SAMPLE_FORMAT, .bitstream_version = 1,
        .buffers = BUFFERS, .block_bytes = BLOCK_BYTES, .mapping_bytes = MAPPING_BYTES
    };
    struct osc_capture_block block = { .bytes = s->payload_bytes, .data_offset = HEADER_BYTES,
        .sample_rate_hz = s->sample_rate, .trigger_sample = U64_MAX,
        .flags = s->peak_detect ? OSC_CAPTURE_PEAKS : 0 };
    struct capture_slot *slot = NULL;
    unsigned i, index;
    unsigned long flags;
    int error = 0;
    if (command == OSC_CAPTURE_GET_INFO) return copy_to_user(user, &info, sizeof(info)) ? -EFAULT : 0;
    if (command != OSC_CAPTURE_DEQUEUE && command != OSC_CAPTURE_RELEASE &&
        command != OSC_CAPTURE_CONFIGURE && command != OSC_CAPTURE_TEST_CLOCK &&
        command != OSC_CAPTURE_SET_PROBE) return -ENOTTY;
    if (mutex_lock_interruptible(&s->ioctl_lock)) return -ERESTARTSYS;
    if(command==OSC_CAPTURE_SET_PROBE){
        struct osc_capture_probe probe;
        if(copy_from_user(&probe,user,sizeof(probe))){error=-EFAULT;goto unlock;}
        if(probe.control&~OSC_CAPTURE_PROBE_MASK){error=-EINVAL;goto unlock;}
        spin_lock_irqsave(&s->lock,flags);
        for(i=0;i<BUFFERS;++i)if(s->slot[i].state==LEASED)error=-EBUSY;
        spin_unlock_irqrestore(&s->lock,flags);
        if(!error)s->probe_control=probe.control;
        goto unlock;
    }
    if (command == OSC_CAPTURE_CONFIGURE || command == OSC_CAPTURE_TEST_CLOCK) {
        struct osc_capture_config config;
        struct osc_capture_test_clock test;
        u32 payload_shift, rate, control;
        if (command == OSC_CAPTURE_CONFIGURE) {
            if (copy_from_user(&config, user, sizeof(config))) { error = -EFAULT; goto unlock; }
            if (!osc_capture_rate(config.rate_index) || config.payload_shift > 17) { error = -EINVAL; goto unlock; }
            payload_shift=config.payload_shift; rate=osc_capture_rate(config.rate_index);
            control=(config.rate_index << 1) | (s->probe_control << 14);
        } else {
            if (copy_from_user(&test, user, sizeof(test))) { error = -EFAULT; goto unlock; }
            if (test.shift>6 || test.payload_shift>17) { error = -EINVAL; goto unlock; }
            payload_shift=test.payload_shift; rate=200000000 >> test.shift;
            control=test.shift << 11;
        }
        spin_lock_irqsave(&s->lock, flags);
        for (i = 0; i < BUFFERS; ++i) if (s->slot[i].state == LEASED) error = -EBUSY;
        spin_unlock_irqrestore(&s->lock, flags);
        if (error) goto unlock;
        stop_session(s);
        s->payload_bytes = 8u << payload_shift;
        s->sample_rate = rate;
        s->control = control | (payload_shift << 6);
        s->peak_detect = !!(control & 0x3e);
        writel(s->control, s->receiver->gpio);
        s->inflight = 0; s->next_sequence = 0; s->error = false;
        for (i = 0; i < BUFFERS; ++i) {
            error = submit_slot(&s->slot[i]);
            if (error) { stop_session(s); goto unlock; }
        }
        s->last_completion = jiffies;
        s->stopped = false;
        dma_async_issue_pending(s->receiver->chan);
        writel(s->control | 1, s->receiver->gpio);
        schedule_delayed_work(&s->monitor, HZ / 10);
        goto unlock;
    }
    if (command == OSC_CAPTURE_RELEASE && copy_from_user(&index, user, sizeof(index))) {
        error = -EFAULT; goto unlock;
    }
    spin_lock_irqsave(&s->lock, flags);
    if (s->error || s->stopped) { error = -EIO; goto out; }
    if (command == OSC_CAPTURE_RELEASE) {
        if (index >= BUFFERS || s->slot[index].state != LEASED) error = -EINVAL;
        else error = submit_slot(&s->slot[index]);
        if (error == -EIO) capture_error(s, "cannot queue released DMA buffer");
    } else {
        for (i = 0; i < BUFFERS; i++)
            if (s->slot[i].state == READY && (!slot || s->slot[i].sequence < slot->sequence))
                slot = &s->slot[i];
        if (!slot) { error = -EAGAIN; goto out; }
        slot->state = LEASED;
        block.index = slot->index;
        block.sequence = slot->sequence;
        block.first_sample = slot->first_sample;
        /* Hardware sample counter, relative to stream start, in nanoseconds. */
        {
            u32 remainder;
            block.hardware_ns = div_u64_rem(slot->first_sample, s->sample_rate, &remainder) * 1000000000ULL;
            block.hardware_ns += div_u64((u64)remainder * 1000000000ULL, s->sample_rate);
        }
        if (block.sequence != s->next_sequence) block.flags |= OSC_CAPTURE_DISCONTINUITY;
    }
out:
    spin_unlock_irqrestore(&s->lock, flags);
    if (!error && command == OSC_CAPTURE_DEQUEUE) {
        if (copy_to_user(user, &block, sizeof(block))) {
            spin_lock_irqsave(&s->lock, flags);
            if (submit_slot(slot)) capture_error(s, "cannot requeue DMA buffer after ioctl fault");
            spin_unlock_irqrestore(&s->lock, flags);
            error = -EFAULT;
        } else s->next_sequence = block.sequence + 1;
    }
    dma_async_issue_pending(s->receiver->chan);
unlock:
    mutex_unlock(&s->ioctl_lock);
    return error;
}

static void mapping_open(struct vm_area_struct *vma)
{
    struct session *s = vma->vm_private_data;
    kref_get(&s->ref);
}
static void mapping_close(struct vm_area_struct *vma)
{
    struct session *s = vma->vm_private_data;
    kref_put(&s->ref, session_free);
}
static const struct vm_operations_struct mapping_ops = { .open = mapping_open, .close = mapping_close };
static int capture_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct session *s = file->private_data;
    int error;
    if (vma->vm_pgoff || vma->vm_end - vma->vm_start != MAPPING_BYTES ||
        (vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED)) return -EINVAL;
    vm_flags_clear(vma, VM_MAYWRITE);
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY);
    error = dma_mmap_coherent(s->receiver->chan->device->dev, vma, s->memory, s->dma, MAPPING_BYTES);
    if (error) return error;
    vma->vm_ops = &mapping_ops;
    vma->vm_private_data = s;
    mapping_open(vma);
    return 0;
}
static const struct file_operations capture_ops = {
    .owner = THIS_MODULE, .open = capture_open, .release = capture_release,
    .flush = capture_flush,
    .unlocked_ioctl = capture_ioctl, .compat_ioctl = capture_ioctl,
    .poll = capture_poll, .mmap = capture_mmap, .llseek = no_llseek
};
static int capture_probe(struct platform_device *device)
{
    struct receiver *r;
    int error;
    r = devm_kzalloc(&device->dev, sizeof(*r), GFP_KERNEL);
    if (!r) return -ENOMEM;
    r->gpio = devm_platform_ioremap_resource(device, 0);
    if (IS_ERR(r->gpio)) return PTR_ERR(r->gpio);
    if ((readl(r->gpio + GPIO_STATUS) & 0xffff0000) != PL_ID) return -ENODEV;
    r->chan = dma_request_chan(&device->dev, "rx");
    if (IS_ERR(r->chan)) return dev_err_probe(&device->dev, PTR_ERR(r->chan), "capture DMA\n");
    r->misc = (struct miscdevice){ .minor = MISC_DYNAMIC_MINOR, .name = "oscill-capture",
        .fops = &capture_ops, .parent = &device->dev, .mode = 0600 };
    error = misc_register(&r->misc);
    if (error) dma_release_channel(r->chan);
    return error;
}
static const struct of_device_id capture_match[] = { { .compatible = "oscill,ad9288-capture" }, {} };
static struct platform_driver capture_driver = {
    .probe = capture_probe,
    .driver = { .name = "oscill-capture", .of_match_table = capture_match, .suppress_bind_attrs = true }
};
builtin_platform_driver(capture_driver);
MODULE_LICENSE("GPL");
