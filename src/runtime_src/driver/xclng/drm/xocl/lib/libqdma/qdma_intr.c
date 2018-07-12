/*******************************************************************************
 *
 * Xilinx XDMA IP Core Linux Driver
 * Copyright(c) 2015 - 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "LICENSE".
 *
 * Sujatha Banoth <sbanoth@xilinx.com>
 *
 ******************************************************************************/

#define pr_fmt(fmt)	KBUILD_MODNAME ":%s: " fmt, __func__

#include "qdma_intr.h"

#include <linux/kernel.h>
#include "qdma_descq.h"
#include "qdma_device.h"
#include "qdma_regs.h"
#include "thread.h"
#include "version.h"
#include "qdma_regs.h"

struct qdma_err_info {
	u32 intr_mask;
	char **stat;
};

struct qdma_err_stat_info {
	char *err_name;
	u32 stat_reg_addr;
	struct qdma_err_info err_info;
};

char *glbl_err_info[] =
{
	"err_ram_sbe",
	"err_ram_dbe",
	"err_dsc",
	"err_trq",
	"err_h2c_mm_0",
	"err_h2c_mm_1",
	"err_c2h_mm_0",
	"err_c2h_mm_1",
	"err_c2h_st",
	"ind_ctxt_cmd_err",
	"err_bdg",
	"err_h2c_st"
};

char *dsc_err_info[] =
{
	"poison",
	"ur_ca",
	"param",
	"addr",
	"tag",
	"flr",
	"timeout",
	"dat_poison",
	"flr_cancel",
	"dma",
	"dsc",
	"rq_cancel",
	"dbe",
	"sbe"
};

char *trq_err_info[] =
{
	"unmapped",
	"qid_range",
	"vf_access_err",
	"tcp_timeout"
};

char *c2h_err_info[] =
{
	"mty_mismatch",
	"len_mismatch",
	"qid_mismatch",
	"desc_rsp_err",
	"eng_wpl_data_par_err",
	"msi_int_fail",
	"err_desc_cnt",
	"portid_ctxt_mismatch",
	"portid_byp_in_mismatch",
	"wrb_inv_q_err",
	"wrb_qfull_err"
};

char *c2h_fatal_err_info[] =
{
	"mty_mismatch",
	"len_mismatch",
	"qid_mismatch",
	"timer_fifo_ram_rdbe",
	"eng_wpl_data_par_err",
	"pfch_II_ram_rdbe",
	"wb_ctxt_ram_rdbe",
	"pfch_ctxt_ram_rdbe",
	"desc_req_fifo_ram_rdbe",
	"int_ctxt_ram_rdbe",
	"int_qid2vec_ram_rdbe",
	"wrb_coal_data_ram_rdbe",
	"tuser_fifo_ram_rdbe",
	"qid_fifo_ram_rdbe",
	"payload_fifo_ram_rdbe",
	"wpl_data_par_err"
};

char *h2c_err_info[] =
{
	"no_dma_dsc_err",
	"wbi_mop_err",
	"zero_len_desc_err"
};

struct qdma_err_stat_info err_stat_info[HW_ERRS] =
{
	{ "glbl_err", QDMA_REG_GLBL_ERR_STAT,
			{ QDMA_REG_GLBL_ERR_MASK_VALUE, glbl_err_info } },
	{ "dsc_err", QDMA_GLBL_DSC_ERR_STS,
			{ QDMA_GLBL_DSC_ERR_MSK_VALUE, dsc_err_info } },
	{ "trq_err", QDMA_GLBL_TRQ_ERR_STS,
			{ QDMA_GLBL_TRQ_ERR_MSK_VALUE, trq_err_info } },
	{ "c2h_err", QDMA_REG_C2H_ERR_STAT,
			{ QDMA_REG_C2H_ERR_MASK_VALUE, c2h_err_info } },
	{ "c2h_fatal_err", QDMA_C2H_FATAL_ERR_STAT,
			{ QDMA_C2H_FATAL_ERR_MASK_VALUE, c2h_fatal_err_info } },
	{ "h2c_err", QDMA_H2C_ERR_STAT,
			{ QDMA_H2C_ERR_MASK_VALUE, h2c_err_info } }
};

void err_stat_handler(struct xlnx_dma_dev *xdev)
{
	u32 i;
	u32 j;
	u32 err_stat;
	u32 glb_err_stat = 0;

	for (i = 0; i < HW_ERRS; i++) {
		err_stat = __read_reg(xdev, err_stat_info[i].stat_reg_addr);
		if (i == 0) glb_err_stat = err_stat;
		if (err_stat & err_stat_info[i].err_info.intr_mask) {
			uint8_t bit = 0;
			uint32_t intr_mask =
					err_stat_info[i].err_info.intr_mask;
			uint32_t chk_mask = 0x01;

			pr_info("%s[0x%x] : 0x%x", err_stat_info[i].err_name,
					        err_stat_info[i].stat_reg_addr,
					        err_stat);
			for (j = 0; intr_mask; j++) {
				if (((intr_mask & 0x01)) &&
						(err_stat & chk_mask))
					pr_err("\t%s detected",
					        err_stat_info[i].err_info.stat[bit]);

				if (intr_mask & 0x01) bit++;
				intr_mask >>= 1;
				chk_mask <<= 1;
			}
			__write_reg(xdev, err_stat_info[i].stat_reg_addr,
			            err_stat);
		}
	}
	if (glb_err_stat)
		__write_reg(xdev, err_stat_info[0].stat_reg_addr,
			    glb_err_stat);
}

static irqreturn_t user_intr_handler(int irq_index, int irq, void *dev_id)
{
	struct xlnx_dma_dev *xdev = dev_id;

	pr_info("User IRQ fired on PF#%d: index=%d, vector=%d\n",
		xdev->func_id, irq_index, irq);
	return IRQ_HANDLED;
}

static irqreturn_t error_intr_handler(int irq_index, int irq, void *dev_id)
{
	struct xlnx_dma_dev *xdev = dev_id;
	unsigned long flags;

	pr_info("Error IRQ fired on PF#%d: index=%d, vector=%d\n", xdev->func_id, irq_index, irq);

	spin_lock_irqsave(&xdev->lock, flags);

	err_stat_handler(xdev);

	spin_unlock_irqrestore(&xdev->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t data_intr_handler(int vector_index, int irq, void *dev_id)
{
	struct xlnx_dma_dev *xdev = dev_id;
	struct qdma_descq *descq = NULL;
	unsigned long flags;
	u32 counter = 0;

	pr_debug("Data IRQ fired on PF#%d: index=%d, vector=%d\n",
								xdev->func_id, vector_index, irq);
	spin_lock_irqsave(&xdev->lock, flags);

	if (xdev->intr_coal_en) {
		struct intr_coal_conf *coal_entry = (xdev->intr_coal_list + vector_index - xdev->dvec_start_idx);
		struct qdma_intr_ring *ring_entry;

		if(!coal_entry) {
			pr_err("Failed to locate the coalescing entry for vector = %d\n", vector_index);
			goto irq_hndl;
		}
		pr_debug("INTR_COAL: msix[%d].vector=%d, msix[%d].entry=%d, rngsize=%d, cidx = %d\n",
					vector_index,
					xdev->msix[vector_index].vector,
					vector_index,
					xdev->msix[vector_index].entry,
					coal_entry->intr_rng_num_entries,
					coal_entry->cidx);

		if((xdev->msix[vector_index].entry) ==  coal_entry->vec_id) {
			counter = coal_entry->cidx;
			ring_entry = (coal_entry->intr_ring_base + counter);
			if(!ring_entry) {
				pr_err("Failed to locate the ring entry for vector = %d\n", vector_index);
				goto irq_hndl;
			}
			while(ring_entry->coal_color == coal_entry->color) {
				pr_debug("IRQ[%d]: IVE[%d], Qid = %d, e_color = %d, c_color = %d, intr_type = %d, error_int =%d\n",
							 irq,
							 vector_index,
							 ring_entry->qid,
							 coal_entry->color,
							 ring_entry->coal_color,
							 ring_entry->intr_type,
							 ring_entry->error_int);

				descq = qdma_device_get_descq_by_hw_qid(xdev,
									ring_entry->qid,
									ring_entry->intr_type);
				if (!descq) {
					pr_err("IRQ[%d]: IVE[%d], Qid = %d error_int = %d: desc not found\n",
						 irq,
						 vector_index,
						 ring_entry->qid,
						 ring_entry->error_int);
					goto irq_hndl;
				}

				if(ring_entry->error_int) {
					pr_err("IRQ[%d]: IVE[%d], Qid = %d error_int = %d: interrupt raised due to error\n",
						 irq,
						 vector_index,
						 ring_entry->qid,
						 ring_entry->error_int);
						err_stat_handler(xdev);
				}
				else {
					schedule_work(&descq->work);
				}

				if(++coal_entry->cidx == coal_entry->intr_rng_num_entries) {
					counter = 0;
					xdev->intr_coal_list->color = (xdev->intr_coal_list->color)? 0: 1;
					coal_entry->cidx = 0;
				}
				else
					counter++;

				ring_entry = (coal_entry->intr_ring_base + counter);
			}

			if(descq)
				intr_cidx_update(descq, coal_entry->cidx);

		}
		else {
			pr_err("msix[%d].entry[%d] != vec_id[%d] \n",
						vector_index,
						xdev->msix[vector_index].entry,
						coal_entry->vec_id);
		}

	} else {
		list_for_each_entry(descq, &xdev->intr_list[vector_index], intr_list)
			schedule_work(&descq->work);
	}

irq_hndl:
	spin_unlock_irqrestore(&xdev->lock, flags);

	return IRQ_HANDLED;
}


static inline void intr_ring_free(struct xlnx_dma_dev *xdev, int ring_sz,
			int intr_desc_sz, u8 *intr_desc, dma_addr_t desc_bus)
{
	unsigned int len = ring_sz * intr_desc_sz;

	pr_debug("free %u(0x%x)=%d*%u, 0x%p, bus 0x%llx.\n",
		len, len, intr_desc_sz, ring_sz, intr_desc, desc_bus);

	dma_free_coherent(&xdev->conf.pdev->dev, ring_sz * intr_desc_sz,
			intr_desc, desc_bus);
}

static void *intr_ring_alloc(struct xlnx_dma_dev *xdev, int ring_sz,
				int intr_desc_sz, dma_addr_t *bus)
{
	unsigned int len = ring_sz * intr_desc_sz ;
	u8 *p = dma_alloc_coherent(&xdev->conf.pdev->dev, len, bus, GFP_KERNEL);

	if (!p) {
		pr_err("%s, OOM, sz ring %d, intr_desc %d.\n",
			xdev->conf.name, ring_sz, intr_desc_sz);
		return NULL;
	}

	memset(p, 0, len);

	pr_debug("alloc %u(0x%x)=%d*%u, bus 0x%llx .\n",
		len, len, intr_desc_sz, ring_sz, *bus);

	return p;
}

void intr_ring_teardown(struct xlnx_dma_dev *xdev)
{
	int i = 0;
	struct intr_coal_conf  *ring_entry;
	int ring_index = 0;

#ifndef __QDMA_VF__
	int rv = 0;
#endif

	while (i < QDMA_DATA_VEC_PER_PF_MAX ) {
		ring_index = get_intr_ring_index(xdev, (i + xdev->dvec_start_idx));
		ring_entry = (xdev->intr_coal_list + i);
		if(ring_entry) {
			intr_ring_free(xdev,
						ring_entry->intr_rng_num_entries,
						sizeof(struct qdma_intr_ring),
						(u8 *)ring_entry->intr_ring_base,
						ring_entry->intr_ring_bus);
#ifndef __QDMA_VF__
			pr_debug("Clearing intr_ctxt for ring_index =%d\n", ring_index);
			/* clear interrupt context (0x8) */
			rv = hw_indirect_ctext_prog(xdev, ring_index, QDMA_CTXT_CMD_CLR,
							QDMA_CTXT_SEL_COAL, NULL, 0, 0);
			if (rv < 0) {
				pr_err("Failed to clear interrupt context, rv = %d\n", rv);
			}
#endif
		}
		i++;
	}

	if(xdev->intr_coal_list)
		kfree(xdev->intr_coal_list);
	pr_debug("dev %s interrupt coalescing ring teardown successful\n",
				dev_name(&xdev->conf.pdev->dev));
}

static irqreturn_t irq_top(int irq, void *dev_id)
{
	struct xlnx_dma_dev *xdev = dev_id;

	if (xdev->conf.fp_q_isr_top_dev)
		xdev->conf.fp_q_isr_top_dev((unsigned long)xdev, xdev->conf.uld);
        return IRQ_WAKE_THREAD;
}

static irqreturn_t irq_bottom(int irq, void *dev_id)
{
	struct xlnx_dma_dev *xdev = dev_id;
	int i;

	for (i = 0; i < xdev->num_vecs; i++) {
		if(xdev->msix[i].vector == irq) {
			return xdev->intr_vec_map[i].intr_handler(i, irq, dev_id);
		}
	}

	return IRQ_HANDLED;
}

void intr_teardown(struct xlnx_dma_dev *xdev)
{
	int i = xdev->num_vecs;

	while (--i >= 0)
		free_irq(xdev->msix[i].vector, xdev);

	if (xdev->num_vecs)
		pci_disable_msix(xdev->conf.pdev);
}

int intr_setup(struct xlnx_dma_dev *xdev)
{
	int rv = 0;
	int i;

	if (xdev->conf.poll_mode) {
		pr_info("Polled mode configured, skipping interrupt setup\n");
		return 0;
	}

	xdev->num_vecs = pci_msix_vec_count(xdev->conf.pdev);

	if (!xdev->num_vecs) {
		pr_info("MSI-X not supported, running in polled mode\n");
		return 0;
	}

	if (xdev->num_vecs > XDEV_NUM_IRQ_MAX)
		xdev->num_vecs = XDEV_NUM_IRQ_MAX;

	for (i = 0; i < xdev->num_vecs; i++) {
		xdev->msix[i].entry = i;
		INIT_LIST_HEAD(&xdev->intr_list[i]);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	rv = pci_enable_msix_exact(xdev->conf.pdev, xdev->msix, xdev->num_vecs);
#else
	rv = pci_enable_msix(xdev->conf.pdev, xdev->msix, xdev->num_vecs);
#endif
	if (rv < 0) {
		pr_err("Error enabling MSI-X (%d)\n", rv);
		goto exit;
	}

	for (i = 0; i < xdev->num_vecs; i++) {
		pr_info("Requesting IRQ vector %d\n", xdev->msix[i].vector);

		rv = request_threaded_irq(xdev->msix[i].vector, irq_top,
					irq_bottom, 0, xdev->mod_name, xdev);

		if (rv) {
			pr_err("request_irq for vector %d fail\n", i);
			goto cleanup_irq;
		}

		/** On PF0, vector#0 is dedicated for Error interrupts and
		  * vector #1 is dedicated for User interrupts
		  * For all other PFs, vector#0 is dedicated for User interrupts
		  * Check whether the received interrupts is User interrupt, Error interrupt
		  * or Data interrupt
		  */
		if(i == 0)
		{
			if(xdev->func_id == 0) {
				xdev->intr_vec_map[i].intr_type = INTR_TYPE_ERROR;
				xdev->intr_vec_map[i].intr_vec_index = i;
				xdev->intr_vec_map[i].intr_handler = error_intr_handler;
			}
			else {
				xdev->intr_vec_map[i].intr_type = INTR_TYPE_USER;
				xdev->intr_vec_map[i].intr_vec_index = i;
				xdev->intr_vec_map[i].intr_handler = user_intr_handler;
			}
		}
		else if(i == 1)
		{
			if(xdev->func_id == 0){
				xdev->intr_vec_map[i].intr_type = INTR_TYPE_USER;
				xdev->intr_vec_map[i].intr_vec_index = i;
				xdev->intr_vec_map[i].intr_handler = user_intr_handler;
			}
			else {
				xdev->intr_vec_map[i].intr_type = INTR_TYPE_DATA;
				xdev->intr_vec_map[i].intr_vec_index = i;
				xdev->intr_vec_map[i].intr_handler = data_intr_handler;
			}
		}
		else {
			xdev->intr_vec_map[i].intr_type = INTR_TYPE_DATA;
			xdev->intr_vec_map[i].intr_vec_index = i;
			xdev->intr_vec_map[i].intr_handler = data_intr_handler;
		}
	}

	if(xdev->func_id == 0)
		xdev->dvec_start_idx = 2;
	else
		xdev->dvec_start_idx = 1;

	xdev->flags |= XDEV_FLAG_IRQ;
	return rv;

cleanup_irq:
	while (--i >= 0)
		free_irq(xdev->msix[i].vector, xdev);

	pci_disable_msix(xdev->conf.pdev);

exit:
	return rv;
}

int intr_ring_setup(struct xlnx_dma_dev *xdev)
{

	int num_entries = 0;
	int counter = 0;
	struct intr_coal_conf  *intr_coal_list;
	struct intr_coal_conf  *intr_coal_list_entry;

	if (xdev->conf.poll_mode || !xdev->conf.intr_agg) {
		pr_info("skipping interrupt aggregation: poll %d, agg %d\n",
			xdev->conf.poll_mode, xdev->conf.intr_agg);
		xdev->intr_coal_en = 0;
		xdev->intr_coal_list = NULL;
		return 0;
	}

	if((xdev->num_vecs != 0) && (xdev->num_vecs < xdev->conf.qsets_max)) {
		pr_info("dev %s num_vectors[%d] < num_queues [%d]\n",
					dev_name(&xdev->conf.pdev->dev),
					xdev->num_vecs,
					xdev->conf.qsets_max);
		pr_info("Enabling Interrupt aggregation\n");

		xdev->intr_coal_en = 1;
		/* obtain the number of queue entries in each inr_ring based on ring size */
		num_entries = ((xdev->conf.intr_rngsz + 1) * 512);

		pr_debug("%s interrupt coalescing ring with %d entries \n",
			dev_name(&xdev->conf.pdev->dev), num_entries);
		/*
		 * Initially assuming that each vector has the same size of the
		 * ring, In practical it is possible to have different ring
		 * size of different vectors (?)
	 	 */
		intr_coal_list = kzalloc(
				sizeof(struct intr_coal_conf) * QDMA_DATA_VEC_PER_PF_MAX,
				GFP_KERNEL);
		if (!intr_coal_list) {
			pr_err("dev %s num_vecs %d OOM.\n",
				dev_name(&xdev->conf.pdev->dev), QDMA_DATA_VEC_PER_PF_MAX);
			return -ENOMEM;
		}

		for(counter = 0; counter < QDMA_DATA_VEC_PER_PF_MAX ; counter++) {
			intr_coal_list_entry = (intr_coal_list + counter);
			intr_coal_list_entry->intr_rng_num_entries = num_entries;
			intr_coal_list_entry->intr_ring_base = intr_ring_alloc(
					xdev, num_entries,
					sizeof(struct qdma_intr_ring),
					&intr_coal_list_entry->intr_ring_bus);
			if (!intr_coal_list_entry->intr_ring_base) {
				pr_err("dev %s, sz %u, intr_desc ring OOM.\n",
					xdev->conf.name,
					intr_coal_list_entry->intr_rng_num_entries);
				goto err_out;
			}

			intr_coal_list_entry->vec_id =
					xdev->msix[counter + xdev->dvec_start_idx].entry;
			intr_coal_list_entry->pidx = 0;
			intr_coal_list_entry->cidx = 0;
			intr_coal_list_entry->color = 1;
			pr_debug("ring_index = %d, vector_index = %d, ring_size = %d, ring_base = 0x%08x",
					counter, intr_coal_list_entry->vec_id,
					intr_coal_list_entry->intr_rng_num_entries,
					(unsigned int)intr_coal_list_entry->intr_ring_bus);
		}

		pr_debug("dev %s interrupt coalescing ring setup successful\n",
					dev_name(&xdev->conf.pdev->dev));

		xdev->intr_coal_list = intr_coal_list;
	} else 	{
		pr_info("dev %s intr vec[%d] >= queues[%d], No aggregation\n",
			dev_name(&xdev->conf.pdev->dev), xdev->num_vecs,
			xdev->conf.qsets_max);
		xdev->intr_coal_en = 0;
		xdev->intr_coal_list = NULL;
	}
	return 0;

err_out:
	while(--counter >= 0) {
		intr_coal_list_entry = (intr_coal_list + counter);
		intr_ring_free(xdev, intr_coal_list_entry->intr_rng_num_entries,
				sizeof(struct qdma_intr_ring),
				(u8 *)intr_coal_list_entry->intr_ring_base,
				intr_coal_list_entry->intr_ring_bus);
	}
	kfree(intr_coal_list);
	return -ENOMEM;
}

void intr_work(struct work_struct *work)
{
	struct qdma_descq *descq;

	descq = container_of(work, struct qdma_descq, work);
	qdma_descq_service_wb(descq);
}

/*
 * qdma_queue_service - service the queue
 * 	in the case of irq handler is registered by the user, the user should
 * 	call qdma_queue_service() in its interrupt handler to service the queue
 * @dev_hndl: hndl retured from qdma_device_open()
 * @qhndl: hndl retured from qdma_queue_add()
 */
void qdma_queue_service(unsigned long dev_hndl, unsigned long id)
{
	struct xlnx_dma_dev *xdev = (struct xlnx_dma_dev *)dev_hndl;
	struct qdma_descq *descq = qdma_device_get_descq_by_id(xdev, id,
							NULL, 0, 1);

	if (descq)
        	qdma_descq_service_wb(descq);
}

static u8 get_intr_vec_index(struct xlnx_dma_dev *xdev, u8 intr_type)
{
	u8 i = 0;

	for (i = 0; i < xdev->num_vecs; i++) {
		if(xdev->intr_vec_map[i].intr_type == intr_type)
			return xdev->intr_vec_map[i].intr_vec_index;
	}
	return 0;
}

void qdma_err_intr_setup(struct xlnx_dma_dev *xdev)
{
	u32 val = 0;
	u8  err_intr_index = 0;
	u8 i;

	val = xdev->func_id;
	err_intr_index = get_intr_vec_index(xdev, INTR_TYPE_ERROR);
	val |= V_QDMA_C2H_ERR_INT_VEC(err_intr_index);

	if (xdev->intr_coal_en)
		val |= (1 << S_QDMA_C2H_ERR_INT_F_EN_COAL);

	val |= (1 << S_QDMA_C2H_ERR_INT_F_ERR_INT_ARM);

	__write_reg(xdev, QDMA_C2H_ERR_INT, val);

	pr_debug("Error interrupt setup: val = 0x%08x,  readback =  0x%08x err_intr_index = %d func_id = %d\n",
					val, __read_reg(xdev, QDMA_C2H_ERR_INT),
					err_intr_index, xdev->func_id);

	for (i = 0; i < HW_ERRS; i++)
		qdma_enable_hw_err(xdev, i);

	return;
}


void qdma_enable_hw_err(struct xlnx_dma_dev *xdev, u8 hw_err_type)
{

	switch (hw_err_type) {
	case GLBL_ERR:
		__write_reg(xdev, QDMA_REG_GLBL_ERR_MASK, QDMA_REG_GLBL_ERR_MASK_VALUE);

		pr_debug("Global interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_REG_GLBL_ERR_MASK, __read_reg(xdev, QDMA_REG_GLBL_ERR_MASK));
		break;
	case GLBL_DSC_ERR:
		__write_reg(xdev, QDMA_GLBL_DSC_ERR_MSK, QDMA_GLBL_DSC_ERR_MSK_VALUE);

		pr_debug("Global dsc interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_GLBL_DSC_ERR_MSK, __read_reg(xdev, QDMA_GLBL_DSC_ERR_MSK));
		break;
	case GLBL_TRQ_ERR:
		__write_reg(xdev, QDMA_GLBL_TRQ_ERR_MSK, QDMA_GLBL_TRQ_ERR_MSK_VALUE);

		pr_debug("Global trq interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_GLBL_TRQ_ERR_MSK, __read_reg(xdev, QDMA_GLBL_TRQ_ERR_MSK));
		break;
	case C2H_ERR:
		__write_reg(xdev, QDMA_REG_C2H_ERR_MASK, QDMA_REG_C2H_ERR_MASK_VALUE);

		pr_debug("C2H interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_REG_C2H_ERR_MASK, __read_reg(xdev, QDMA_REG_C2H_ERR_MASK));
		break;
	case C2H_FATAL_ERR:
		__write_reg(xdev, QDMA_C2H_FATAL_ERR_MASK, QDMA_C2H_FATAL_ERR_MASK_VALUE);

		pr_debug("C2H fatal interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_C2H_FATAL_ERR_MASK, __read_reg(xdev, QDMA_C2H_FATAL_ERR_MASK));
		break;
	case H2C_ERR:
		__write_reg(xdev, QDMA_H2C_ERR_MASK, QDMA_H2C_ERR_MASK_VALUE);

		pr_debug("C2H interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_H2C_ERR_MASK, __read_reg(xdev, QDMA_H2C_ERR_MASK));
		break;
	default:
		__write_reg(xdev, QDMA_REG_GLBL_ERR_MASK, QDMA_REG_GLBL_ERR_MASK_VALUE);

		pr_debug("Global interrupts enabled: reg -> 0x%08x,  value =  0x%08x \n",
						QDMA_REG_GLBL_ERR_MASK, __read_reg(xdev, QDMA_REG_GLBL_ERR_MASK));
		break;
	}

	return;
}


int get_intr_ring_index(struct xlnx_dma_dev *xdev, u32 vector_index)
{
	int ring_index = 0;

	ring_index = (vector_index - xdev->dvec_start_idx) + (xdev->func_id * QDMA_DATA_VEC_PER_PF_MAX);
	pr_debug("func_id = %d, vector_index = %d, ring_index = %d\n",
			xdev->func_id, vector_index, ring_index );

	return ring_index;
}