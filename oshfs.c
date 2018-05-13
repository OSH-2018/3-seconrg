#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK 131072

static const size_t size=4*1024*1024*(size_t)1024;
static void * mem[32*1024];
static const size_t blocksize=BLOCK;
time_t rawtime;
int temp=0;
struct filenode
{
        char filename[256];
        int filelen;                            //the last part of the content
        int content[31*1024];
        int begin;                              //the number of last part that been filled
        struct filenode *next;
        struct stat st;
        int where;
};
static struct filenode *get_filenode(const char *name)          //寻找和name名字一致的文件节点，并将其return，找不到，返回空
//在fileattr等函数中调用
{
    struct filenode *root = (struct filenode *)mem[0];
    struct filenode *node = root->next;
    while(node)
    {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

int findagap(int k)                         //find from the last place where it is found (kongjianjubuxing)
{
    int i;
    int *memory;
    memory=(int *)mem[1];
    for (i=k;i<32*1024;i++)
        if (memory[i]==0)
        {
            temp=i;
            memory[i]=1;
            return i;
        }
    for (i=k-1;i>0;i--)
        if (memory[i]==0)
        {
            temp=i;
            memory[i]=1;
            return i;
        }
    return i;
}

int deleteamem(int i)
{
    munmap(mem[i], blocksize);              //munmap可以释放一部分空间
    //取消对应的内存映射
    int *memory;
    memory=(int *)mem[1];
    memory[i]=0;
    mem[i]=NULL;
    return 1;
}


static void create_filenode(const char *filename, const struct stat *st)        //创造一个新的文件节点（在mknod中调用）
{
    int place;
    place=findagap(temp);
    if (place==0)
    {
        printf("not enough space!\n");
        return ;
    }
    printf("create filenode\n");
    mem[place]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    struct filenode *newer = (struct filenode *)mem[place];
    //为新生成的文件节点分配空间
    //为filename分配空间
    printf("mmap\n");
    memcpy(newer->filename, filename, strlen(filename)+1);
    //将filenamecopy到对应的节点下面
    //新节点的文件属性结构体分配空间
    printf("%d\n",place);
    struct filenode *root = (struct filenode *)mem[0];
    struct filenode *p;
    memcpy(&(newer->st), st, sizeof( struct stat));
    printf("st\n");
    //将st的内容copy到结构体的节点下面
    if (root->next==NULL)
        root->next=newer;
    else
    {
        p=root->next;
        newer->next=p;
        root->next=newer;
    }
    newer->begin=0;
    newer->where=place;
    newer->filelen=0;
    //从前面拼接
    newer->content[0] =-1;

}

static int oshfs_truncate(const char *path, off_t size)     //用于修改文件的大小
{
    int num,i;
    struct filenode *node = get_filenode(path);             //打开文件
    if (node==NULL)
    {
        printf("the path doesn't exist!\n");
        return 0;
    }
    time(&rawtime);
    node->st.st_ctime=rawtime;
    node->st.st_mtime=rawtime;
    if (size>=node->st.st_size) return 0;
    node->st.st_size = size;                               //文件的大小设定为size大小
    num=size/BLOCK;          // the number of blocks
    node->begin=size%BLOCK;
    for (i=num+1;i<node->filelen;i++)                     //
        deleteamem(node->content[i]);
    node->filelen=num+1;
    printf("This is truncate\n");
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//将缓冲区（buf）中的数据写到一个打开的文件中
//重新分配内存
{
    struct filenode *node = get_filenode(path);                     //打开文件
    if (node==NULL)
    {
        printf("this path doesn't exist!\n");
        return 0;
    }
                                    // now there is one
    node->st.st_size = offset + size;                              //修改文件的大小标志
    int count=(size+offset-1)/BLOCK+1;                                     //the number of blocks that is needed,[(size+offset)/BLOCK]+1
    time(&rawtime);
    node->st.st_ctime=rawtime;
    node->st.st_mtime=rawtime;
    int place;                                      //the target mem
    int off=0;
    int i,j,remain;
    int a1=offset/BLOCK;
    int a2=offset%BLOCK;
    //printf("%d,%d\n",a1,a2);
    //printf("%s",buf);
    if (a1<node->filelen)
        memcpy((char *)mem[node->content[a1]]+a2,buf,BLOCK-a2);
    if (a1>=node->filelen)
    {
        place=findagap(temp);
        if (place==0)
        {
            printf("not enough space!\n");
            return 0;
        }
        for (j=node->filelen;j<=a1;j++)
        {
            mem[place]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memset(mem[place], ' ', BLOCK);
            if (j==a1)
                memcpy((char *)mem[place]+a2,buf,BLOCK-a2);
            //printf("really?\n");
            node->content[j]=place;
            node->filelen++;
        }
    }
    i=a1+1;
    count=count-i;
    off=BLOCK-a2;
    int num=node->filelen;                          //get the number of the existing file
    for (;i<num;i++)                                  //the mem have been mmaped,so
    {
        if (count>1)
        {
            memcpy((char*)mem[node->content[i]], buf+off, BLOCK);
            off+=BLOCK;
            count--;
        }
        if (count==1)
        {
            remain=(size+offset)%BLOCK;
            memcpy((char*)mem[node->content[i]], buf+off, remain);
            node->begin=remain;
            return size;                            //work finished num>=count
        }
    }
    while (count>0)
    {
        if (count==1)
        {
            place=findagap(temp);
            if (place==0)
            {
                printf("not enough space!\n");
                return 0;
            }
            mem[place]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memset(mem[place], ' ', BLOCK);
            memcpy((char *)mem[place],buf+off,remain);
            node->content[i]=place;
            node->begin=remain;
            node->filelen++;
            //printf("over!\n");
            return size;                            //work finished num>=count
        }
        else
        {
            place=findagap(temp);
            if (place==0)
            {
                printf("not enough space!\n");
                return 0;
            }
            mem[place]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memset(mem[place], ' ', BLOCK);
            memcpy((char *)mem[place],buf+off,BLOCK);
            node->content[i]=place;
            node->filelen++;
            off+=BLOCK;
            count--;
        }
    }//重新为文件内容分配空间
    //printf("END!\n");
    return size;
}


static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//从一个已经打开的文件中读出数据
{
    struct filenode *node = get_filenode(path);                     //寻找对应的节点
    if (node==NULL)
    {
        printf("It doesn't exist!\n");
        return 0;
    }
    int ret = size;
    int k;
    if(offset + size > node->st.st_size)                           //如果读取的数据的大小超过了文件的总大小
        ret = node->st.st_size - offset;                           //那么实际读取的数值的数量为文件大小减去偏移量                                                 //ret初值为读取数据的大小
    int i,off=0;
    int a1=offset/BLOCK;
    int a2=offset%BLOCK;
    time(&rawtime);
    node->st.st_atime=rawtime;
    //printf("%d,%d,%d,%d\n",node->content[a1],a1,a2,BLOCK-a2);
    //printf("here\n");
    while (ret>off)
    {
        if (ret-off>BLOCK-a2) k=BLOCK-a2;
		else k=ret-off;
		memcpy(buf+off,mem[node->content[a1]]+a2,k);
		off+=k;
		a2=0;
        a1++;
    }
    return ret;                                                     //返回读取数据的大小
}



//all the following remained same as the example



static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)            //创造一个节点
{
    struct stat st;                                                         //定义一个状态结构体
    st.st_mode = S_IFREG | 0644;                                            //保护模式定义为普通文件
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;                                                        //硬链接个数设置为1个
    st.st_size = 0;
    time(&rawtime);                                                         //初始的文件大小为0
    st.st_atime=rawtime;
    st.st_ctime=rawtime;
    st.st_mtime=rawtime;
    create_filenode(path + 1, &st);                                         //调用了创造文件节点的函数，并将路径和定义的st和传进去
    return 0;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)  //path : 如 /ab/cd/e不是真正的根，访问a文件：/a,所以第一个/必须去除
//stbuf 缓冲区（需要拷贝到的目标）
{
    int ret = 0;
    struct filenode *node = get_filenode(path);         //调用get_filenode函数，寻找与路径一致的文件节点
    if(strcmp(path, "/") == 0)
    {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;                //缓冲区中的文件保护模式设定为目录
    }
    else if(node)                                       //如果节点存在，那么就把节点的文件属性copy到属性缓冲区中
        memcpy(stbuf, &(node->st), sizeof(struct stat));
    else                                                //若不存在，那么就返回错误
        ret = -ENOENT;
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
//读出所有文件的信息
{
    struct filenode *root = (struct filenode *)mem[0];
    struct filenode *node = root->next;
    filler(buf, ".", NULL, 0);                  //调用filler函数去填充buff
    filler(buf, "..", NULL, 0);
    while(node)
    {                                       //依次读出每个文件的信息
        filler(buf, node->filename, &(node->st), 0);
        node = node->next;
    }
    return 0;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    mem[0]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    struct filenode *newer = (struct filenode *)mem[0];
    strcpy(newer->filename,"root");
    newer->next=NULL;
    newer->where=0;
    mem[1]=mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int *memory=(int *)mem[1];
    int i;
    for (i=2;i<32*1024;i++)
        memory[i]=0;
    memory[0]=1;
    memory[1]=1;
    time(&rawtime);                                                         //初始的文件大小为0
    newer->st.st_atime=rawtime;
    newer->st.st_ctime=rawtime;
    newer->st.st_mtime=rawtime;
    printf("init right\n");
    return NULL;
}
static int oshfs_open(const char *path, struct fuse_file_info *fi)          //打开文件
{
    return 0;
}

static int oshfs_unlink(const char *path)               //用于删除一个节点
{
    struct filenode *root = (struct filenode *)mem[0];
    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root;
    if (node1)                         //若node1存在
    {
        while(node2->next!=node1&&node2!=NULL)
            node2 = node2->next;
        node2->next=node1->next;
        node1->next=NULL;
    }
    else return -ENOENT;
    int i;
    for (i=0;i<node1->filelen;i++)
        deleteamem(node1->content[i]);
    printf("%d\n",node1->where);
    deleteamem(node1->where);
    printf("asdf\n");
    return 0;
    //开始删除node1节点

}

static const struct fuse_operations op = {              //不同的op所对应的函数
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);            //调用fuse函数
}
