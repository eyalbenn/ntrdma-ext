#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <rdma/ib_umem.h>
#include <linux/slab.h>
#include <linux/ntc.h>

#define DRIVER_NAME "ntc_phys"
#define DRIVER_VERSION  "0.2"
#define DRIVER_RELDATE  "30 September 2015"

MODULE_AUTHOR("Allen Hubbe");
MODULE_DESCRIPTION("NTC physical channel-mapped buffer support library");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);

static void *ntc_phys_buf_alloc(struct ntc_dev *ntc, u64 size,
				u64 *addr, gfp_t gfp)
{
	struct device *dev = ntc_map_dev(ntc, NTB_DEV_ACCESS);
	dma_addr_t dma_addr;
	void *buf;

	buf = dma_alloc_coherent(dev, size, &dma_addr, gfp);

	/* addr must store at least a platform dma addr */
	BUILD_BUG_ON(sizeof(*addr) < sizeof(dma_addr));
	*addr = (u64)dma_addr;

	return buf;
}

static void ntc_phys_buf_free(struct ntc_dev *ntc, u64 size,
			      void *buf, u64 addr)
{
	struct device *dev = ntc_map_dev(ntc, NTB_DEV_ACCESS);

	dma_free_coherent(dev, size, buf, addr);
}

static u64 ntc_phys_buf_map(struct ntc_dev *ntc, void *buf, u64 size,
			    enum dma_data_direction dir,
				enum ntc_dma_access dma_dev)
{
	u64 dma_handle;
	struct device *dev = ntc_map_dev(ntc, dma_dev);

	/* return value must store at least a platform dma addr */
	BUILD_BUG_ON(sizeof(u64) < sizeof(dma_addr_t));

	dma_handle = dma_map_single(dev, buf, size, dir);

	if (dma_mapping_error(dev, dma_handle))
		return 0;

	return dma_handle;
}

static void ntc_phys_buf_unmap(struct ntc_dev *ntc, u64 addr, u64 size,
			       enum dma_data_direction dir,
				   enum ntc_dma_access dma_dev)
{
	struct device *dev = ntc_map_dev(ntc, dma_dev);

	dma_unmap_single(dev, addr, size, dir);
}

static u64 ntc_phys_res_map(struct ntc_dev *ntc, u64 phys_addr, u64 size,
			    enum dma_data_direction dir,
				enum ntc_dma_access dma_dev)
{
	u64 dma_handle;

	struct device *dev = ntc_map_dev(ntc, dma_dev);

	if (WARN(size == 0 || phys_addr == 0,
			"size %#llx addr %#llx",
			size, phys_addr)) {
		return 0;
	}

	/* return value must store at least a platform dma addr */
	BUILD_BUG_ON(sizeof(u64) < sizeof(dma_addr_t));

	dma_handle = dma_map_resource(dev,
			(phys_addr_t)phys_addr,
			(size_t)size,
			dir, 0);

	if (dma_mapping_error(dev, dma_handle))
		return 0;

	return dma_handle;
}

static void ntc_phys_res_unmap(struct ntc_dev *ntc, u64 dma_addr, u64 size,
			       enum dma_data_direction dir,
				   enum ntc_dma_access dma_dev)
{
	struct device *dev = ntc_map_dev(ntc, dma_dev);

	if (WARN(size == 0 || dma_addr == 0,
			"size %#llx dma addr %#llx",
			size, dma_addr)) {
		return;
	}

	dma_unmap_resource(dev, (dma_addr_t)dma_addr,
			(size_t)size, dir, 0);
}
static void ntc_phys_buf_sync_cpu(struct ntc_dev *ntc, u64 addr, u64 size,
				  enum dma_data_direction dir,
				  enum ntc_dma_access dma_dev)
{
	struct device *dev = ntc_map_dev(ntc, dma_dev);

	dma_sync_single_for_cpu(dev, addr, size, dir);
}

static void ntc_phys_buf_sync_dev(struct ntc_dev *ntc, u64 addr, u64 size,
				  enum dma_data_direction dir,
				  enum ntc_dma_access dma_dev)
{
	struct device *dev = ntc_map_dev(ntc, dma_dev);

	dma_sync_single_for_device(dev, addr, size, dir);
}

struct ntrdma_umem {
	struct ib_umem *ib_umem;
	struct sg_table remote_sg_head;
};

static inline void ntc_sgl_clone(struct scatterlist *sgl_src,
		struct scatterlist *sgl_dst, int count)
{
	struct scatterlist *tmp_sg_src;
	struct scatterlist *tmp_sg_dst;
	int i;

	tmp_sg_src = sgl_src;
	tmp_sg_dst = sgl_dst;

	for (i = 0; i < count; i++) {
		sg_set_page(tmp_sg_dst, sg_page(tmp_sg_src), PAGE_SIZE, 0);
		tmp_sg_src = sg_next(tmp_sg_src);
		tmp_sg_dst = sg_next(tmp_sg_dst);
	}

}

static inline struct ntrdma_umem *ntc_phys_umem_get_put(
		struct ntc_dev *ntc,
		struct ib_ucontext *uctx,
		unsigned long uaddr, size_t size,
		int access, int dmasync,
		struct ntrdma_umem *umem,
		int is_put)
{
	struct ntrdma_umem *ntrdma_umem;
	struct ib_umem *ib_umem;
	int ret;

	if (is_put)
		goto is_put;

	ntrdma_umem = kzalloc(sizeof(*ntrdma_umem), GFP_KERNEL);
	if (!ntrdma_umem)
		return ERR_PTR(-ENOMEM);

	ib_umem = ib_umem_get(uctx, uaddr, size, access, dmasync);

	if (IS_ERR(ntrdma_umem->ib_umem))
		goto err_ib_umem;

	ntrdma_umem->ib_umem = ib_umem;

	ret = sg_alloc_table(&ntrdma_umem->remote_sg_head,
			ib_umem_num_pages(ib_umem), GFP_KERNEL);

	if (ret)
		goto err_sg_alloc;

	ntc_sgl_clone(ib_umem->sg_head.sgl, ntrdma_umem->remote_sg_head.sgl,
			ib_umem->npages);

	ret = dma_map_sg_attrs(ntc_map_dev(ntc, NTB_DEV_ACCESS),
			ntrdma_umem->remote_sg_head.sgl,
			ib_umem->npages,
			DMA_BIDIRECTIONAL,
			dmasync?DMA_ATTR_WRITE_BARRIER:0);

	if (ret <= 0)
		goto err_dma_map;

	return ntrdma_umem;
is_put:
	ntrdma_umem = umem;
	dma_unmap_sg(ntc_map_dev(ntc, NTB_DEV_ACCESS),
			ntrdma_umem->remote_sg_head.sgl,
			ntrdma_umem->ib_umem->npages,
			DMA_BIDIRECTIONAL);
err_dma_map:
	sg_free_table(&ntrdma_umem->remote_sg_head);
err_sg_alloc:
	ib_umem_release(ntrdma_umem->ib_umem);
	ntrdma_umem->ib_umem = 0;
	ib_umem = NULL;
err_ib_umem:
	kfree(ntrdma_umem);
	ntrdma_umem = NULL;
	return ntrdma_umem;
}

static void *ntc_phys_umem_get(struct ntc_dev *ntc, struct ib_ucontext *uctx,
			       unsigned long uaddr, size_t size,
			       int access, int dmasync)
{
	return ntc_phys_umem_get_put(ntc, uctx, uaddr, size,
			access, dmasync, NULL, false);
}

static void ntc_phys_umem_put(struct ntc_dev *ntc, void *umem)
{
	struct ntrdma_umem *ntrdma_umem = umem;

	ntc_phys_umem_get_put(ntc, NULL, 0, 0,
				0, 0, ntrdma_umem, true);
}

static int ntc_compress_sgl(struct sg_table *sg_head,
		size_t offset, size_t length,
		struct ntc_sge *sgl, int count)
{
	struct scatterlist *sg, *next;
	dma_addr_t dma_addr, next_addr;
	size_t dma_len;
	int i, dma_count = 0;

	BUILD_BUG_ON(sizeof(u64) < sizeof(dma_addr));
	BUILD_BUG_ON(sizeof(u64) < sizeof(dma_len));

	for_each_sg(sg_head->sgl, sg, sg_head->nents, i) {
		/* dma_addr is start addr of the contiguous range */
		dma_addr = sg_dma_address(sg);
		/* dma_len accumulates the length of the contiguous range */
		dma_len = sg_dma_len(sg);

		for (; i + 1 < sg_head->nents; ++i) {
			next = sg_next(sg);
			if (!next)
				break;
			next_addr = sg_dma_address(next);
			if (next_addr != dma_addr + dma_len)
				break;
			dma_len += sg_dma_len(next);
			sg = next;
		}

		if (sgl && dma_count < count) {
			sgl[dma_count].addr = dma_addr;
			sgl[dma_count].len = dma_len;
		}

		++dma_count;
	}

	if (dma_count && sgl && count > 0) {
		/* dma_len is start offset in the first page */
		dma_len = offset;
		sgl[0].addr += dma_len;
		sgl[0].len -= dma_len;

		if (dma_count <= count) {
			/* dma_len is offset from the end of the last page */
			dma_len = (dma_len + length) & ~PAGE_MASK;
			dma_len = (PAGE_SIZE - dma_len) & ~PAGE_MASK;
				sgl[dma_count - 1].len -= dma_len;
		}
	}

	return dma_count;
}

static int ntc_phys_umem_sgl(struct ntc_dev *ntc, void *umem,
			     struct ntc_sge *sgl, int count)
{
	struct ntrdma_umem *ntrdma_umem = umem;
	struct ib_umem *ibumem = ntrdma_umem->ib_umem;
	int local_dma_count;
	int remote_dma_count;

	local_dma_count = ntc_compress_sgl(
			&ibumem->sg_head,
			ib_umem_offset(ibumem),
			ibumem->length, sgl, count);


	remote_dma_count = ntc_compress_sgl(
			&ntrdma_umem->remote_sg_head,
			ib_umem_offset(ibumem),
			ibumem->length, sgl + count, count);


	WARN_ON(local_dma_count != remote_dma_count);

	return local_dma_count;
}
struct ntc_map_ops ntc_phys_map_ops = {
	.buf_alloc		= ntc_phys_buf_alloc,
	.buf_free		= ntc_phys_buf_free,
	.buf_map		= ntc_phys_buf_map,
	.buf_unmap		= ntc_phys_buf_unmap,
	.res_map		= ntc_phys_res_map,
	.res_unmap		= ntc_phys_res_unmap,
	.buf_sync_cpu	= ntc_phys_buf_sync_cpu,
	.buf_sync_dev	= ntc_phys_buf_sync_dev,
	.umem_get		= ntc_phys_umem_get,
	.umem_put		= ntc_phys_umem_put,
	.umem_sgl		= ntc_phys_umem_sgl,
};
EXPORT_SYMBOL(ntc_phys_map_ops);
