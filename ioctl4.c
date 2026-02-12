#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/version.h>
static DEFINE_MUTEX(chardev_lock);
enum 
{
	N_USED,
	USED,
};
static atomic_t already_inuse=ATOMIC_INIT(0);
struct ioctl_seek_arg
{
	int cmd_no;
	char __user *buf;
	ssize_t len;
};
struct node_cll;
typedef struct node_cll node;
struct node_cll
{
	char* data;
	int len;
	node* prev;
	node* next;
};
static node* head=NULL; 
static node* tail=NULL;
static int node_count=0;
#define IOCTL_MAGIC 'k'
#define IOCTL_SEEK _IOWR(IOCTL_MAGIC,1,struct ioctl_seek_arg)
#define IOCTL_DESTROY _IO(IOCTL_MAGIC,2)
static int major;
static struct device *chardev;
static struct class *cls;
#define DEVICE_NAME "chardev_ioctl4"
static long chardev_ioctl(struct file* file, unsigned cmd,unsigned long args)
{
	mutex_lock(&chardev_lock);
	ssize_t x=0;
	node* temp;
	struct ioctl_seek_arg k;
	int len=0;
	switch(cmd)
	{
	case IOCTL_SEEK:
	{
		if(!head || node_count)
		{
			pr_info("queue empty");
			x=-ENODATA;
			goto end;
		}
		if(copy_from_user(&k,(struct ioct_seek_arg*)args,sizeof(k)))
		{
			pr_alert("copy of arguments to kernel space failed in IOCTL_SEEK");
			x=-EFAULT;
			goto end;
		}
		if(!k.buf)
		{
			pr_alert("the argument passed is null");
			x=-EINVAL;
			goto end;
		}
		if(k.len <=0)
		{
			pr_alert("the size of buffer is too small");
			x=-EINVAL;
			goto end;
		}
		temp=head;
		if(k.cmd_no<0)
		{
			k.cmd_no=0;
		}
		k.cmd_no=k.cmd_no%node_count;
		for(int i=0;i<k.cmd_no;i++,temp=temp->next);
		len=min((int )k.len,temp->len);
		if(copy_to_user(k.buf,temp->data,len))
		{
			pr_alert("the argument passed is null");
			x=-EFAULT;
			goto end;
		}
		pr_info("copied the contents to user buffer");
		x=0;
		goto end;
	}
	case IOCTL_DESTROY:
	{
		if(!head)
		{
			x=-ENODATA;
			goto end;
		}
		tail->next=NULL;
		while(head)
		{
			temp=head;
			head=head->next;
			kfree(temp->data);
			kfree(temp);
		}
		node_count=0;
		head=NULL; tail=NULL;
		pr_info("circular queue destroyed successfully");
		mutex_unlock(&chardev_lock);
		return 0;
	}
	default:
		goto end;
	}
end:
	mutex_unlock(&chardev_lock);
	return x;
}
static ssize_t chardev_read(struct file* file, char __user* buffer, size_t len, loff_t * offset)
{
	mutex_lock(&chardev_lock);
	char *temp_buffer=NULL;
	int len_k=0;
	node* temp=NULL;
	ssize_t x=0;
	if(!head || node_count ==0)
	{
		pr_alert("queue empty");
		x=-ENODATA;
		goto end;
	}
	temp=head;
	len_k=(int)min((int )len,temp->len);
	if(copy_to_user(buffer,temp->data,len_k))
	{
		pr_alert("failure in copying buffer to userspace");
		x=-EFAULT;
		goto end;
	}
	pr_info("successfully copied buffer to usersapce");
	int remaining=temp->len - len_k;
	if(!remaining)
	{
		kfree(temp->data);
		if(head==tail)
		{
			head=NULL;
			tail=NULL;
			node_count=0;
		}
		else
		{
			head=head->next;
			head->prev=tail;
			tail->next = head;
			node_count--;
		}
		kfree(temp);
	}
	else
	{
		temp_buffer=kmalloc(remaining,GFP_KERNEL);
		if(!temp_buffer)
		{
			pr_alert("malloc failure");
			x=-ENOMEM;
			goto end;
		}
		memcpy(temp_buffer,temp->data+len_k,remaining);
		kfree(temp->data);
		temp->data=temp_buffer;
		temp->len=remaining;
	}
	x=len_k;
end:
	mutex_unlock(&chardev_lock);
	return x;
}
static ssize_t chardev_write(struct file* file, const char __user *buffer, size_t len, loff_t* offset)
{
	mutex_lock(&chardev_lock);
	ssize_t x=0;
	node* temp;
	if(!buffer || len==0)
	{
		pr_alert("input values not set");
		x=-EINVAL;
		goto end;
	}
	if(len > PAGE_SIZE* 16)
	{
		pr_alert("requested data too large");
		x=-EINVAL;
		goto end;
	}
	temp=kmalloc(sizeof(node),GFP_KERNEL);
	if(!temp)
	{
		pr_alert("kmalloc failure");
		x=-ENODATA;
		goto end;
	}
	temp->data=kmalloc(len,GFP_KERNEL);
	if(!temp->data)
	{
		pr_alert("kmalloc failure");
		x=-ENODATA;
		goto end;
	}
	if(!head)
	{
		head=temp;
		tail=temp;
		head->next=tail;
		head->prev=tail;
		node_count=1;
	}
	else
	{
		tail->next=temp;
		head->prev=temp;
		temp->next=head;
		temp->prev=tail;
		tail=temp;
		node_count++;
	}
	x=len;
end:
	mutex_unlock(&chardev_lock);
	return x;
}
static int chardev_open(struct inode* inode, struct file* file)
{
	if(atomic_cmpxchg(&already_inuse,N_USED,USED))
	{
		pr_alert("resource already in use");
		return -EBUSY;
	}
	pr_info("file opened successfully");
	return 0;
}
static int chardev_release(struct inode* inode, struct file* file)
{
	atomic_set(&already_inuse,N_USED);
	pr_info("file closed");
	return 0;
}
static struct file_operations fops={
	.read=chardev_read,
	.write=chardev_write,
	.open=chardev_open,
	.release=chardev_release,
	.unlocked_ioctl=chardev_ioctl,
	.owner=THIS_MODULE,
};
static int __init chardev_init(void)
{
	mutex_lock(&chardev_lock);
	major=register_chrdev(0,DEVICE_NAME,&fops);
	if(major <0)
	{
		pr_alert("failed to register chardev");
		mutex_unlock(&chardev_lock);
		return -EFAULT;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
	cls=class_create(DEVICE_NAME);
#else
	cls=class_create(THIS_MODULE,DEVICE_NAME);
#endif
	if(!cls)
	{
		pr_alert("failed to create class");
		unregister_chrdev(major,DEVICE_NAME);
		mutex_unlock(&chardev_lock);
		return -EFAULT;
	}
	chardev=device_create(cls,NULL,MKDEV(major,0),NULL,DEVICE_NAME);
	if(!chardev)
	{
		pr_alert("failed to create class");
		unregister_chrdev(major,DEVICE_NAME);
		class_destroy(cls);
		mutex_unlock(&chardev_lock);
		return -EFAULT;
	}
	mutex_unlock(&chardev_lock);
	return 0;
}
static void __exit chardev_exit(void)
{
	if(head || node_count)
	{
		node* temp;
		tail->next=NULL;
		while(head)
		{
			temp=head;
			head=head->next;
			kfree(temp->data);
			kfree(temp);
		}
		node_count=0;
	}
	device_destroy(cls,MKDEV(major,0));
	class_destroy(cls);
	unregister_chrdev(major,DEVICE_NAME);
}
module_init(chardev_init);
module_exit(chardev_exit);
MODULE_LICENSE("GPL");