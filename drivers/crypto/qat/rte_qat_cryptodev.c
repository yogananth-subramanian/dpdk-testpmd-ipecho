/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2015-2017 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <rte_common.h>
#include <rte_dev.h>
#include <rte_malloc.h>
#include <rte_cryptodev_pmd.h>
#include <rte_cryptodev_pci.h>

#include "qat_crypto.h"
#include "qat_logs.h"

uint8_t cryptodev_qat_driver_id;

static const struct rte_cryptodev_capabilities qat_gen1_capabilities[] = {
	QAT_BASE_GEN1_SYM_CAPABILITIES,
	RTE_CRYPTODEV_END_OF_CAPABILITIES_LIST()
};

static const struct rte_cryptodev_capabilities qat_gen2_capabilities[] = {
	QAT_BASE_GEN1_SYM_CAPABILITIES,
	QAT_EXTRA_GEN2_SYM_CAPABILITIES,
	RTE_CRYPTODEV_END_OF_CAPABILITIES_LIST()
};

static struct rte_cryptodev_ops crypto_qat_ops = {

		/* Device related operations */
		.dev_configure		= qat_dev_config,
		.dev_start		= qat_dev_start,
		.dev_stop		= qat_dev_stop,
		.dev_close		= qat_dev_close,
		.dev_infos_get		= qat_dev_info_get,

		.stats_get		= qat_crypto_sym_stats_get,
		.stats_reset		= qat_crypto_sym_stats_reset,
		.queue_pair_setup	= qat_crypto_sym_qp_setup,
		.queue_pair_release	= qat_crypto_sym_qp_release,
		.queue_pair_start	= NULL,
		.queue_pair_stop	= NULL,
		.queue_pair_count	= NULL,

		/* Crypto related operations */
		.session_get_size	= qat_crypto_sym_get_session_private_size,
		.session_configure	= qat_crypto_sym_configure_session,
		.session_clear		= qat_crypto_sym_clear_session
};

/*
 * The set of PCI devices this driver supports
 */

static const struct rte_pci_id pci_id_qat_map[] = {
		{
			RTE_PCI_DEVICE(0x8086, 0x0443),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x37c9),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x19e3),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x6f55),
		},
		{.device_id = 0},
};

static int
crypto_qat_dev_init(struct rte_cryptodev *cryptodev)
{
	struct qat_pmd_private *internals;

	PMD_INIT_FUNC_TRACE();
	PMD_DRV_LOG(DEBUG, "Found crypto device at %02x:%02x.%x",
		RTE_DEV_TO_PCI(cryptodev->device)->addr.bus,
		RTE_DEV_TO_PCI(cryptodev->device)->addr.devid,
		RTE_DEV_TO_PCI(cryptodev->device)->addr.function);

	cryptodev->driver_id = cryptodev_qat_driver_id;
	cryptodev->dev_ops = &crypto_qat_ops;

	cryptodev->enqueue_burst = qat_pmd_enqueue_op_burst;
	cryptodev->dequeue_burst = qat_pmd_dequeue_op_burst;

	cryptodev->feature_flags = RTE_CRYPTODEV_FF_SYMMETRIC_CRYPTO |
			RTE_CRYPTODEV_FF_HW_ACCELERATED |
			RTE_CRYPTODEV_FF_SYM_OPERATION_CHAINING |
			RTE_CRYPTODEV_FF_MBUF_SCATTER_GATHER;

	internals = cryptodev->data->dev_private;
	internals->max_nb_sessions = RTE_QAT_PMD_MAX_NB_SESSIONS;
	switch (RTE_DEV_TO_PCI(cryptodev->device)->id.device_id) {
	case 0x0443:
		internals->qat_dev_gen = QAT_GEN1;
		internals->qat_dev_capabilities = qat_gen1_capabilities;
		break;
	case 0x37c9:
	case 0x19e3:
	case 0x6f55:
		internals->qat_dev_gen = QAT_GEN2;
		internals->qat_dev_capabilities = qat_gen2_capabilities;
		break;
	default:
		PMD_DRV_LOG(ERR,
			"Invalid dev_id, can't determine capabilities");
		break;
	}

	/*
	 * For secondary processes, we don't initialise any further as primary
	 * has already done this work. Only check we don't need a different
	 * RX function
	 */
	if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
		PMD_DRV_LOG(DEBUG, "Device already initialised by primary process");
		return 0;
	}

	return 0;
}

static int crypto_qat_pci_probe(struct rte_pci_driver *pci_drv __rte_unused,
	struct rte_pci_device *pci_dev)
{
	return rte_cryptodev_pci_generic_probe(pci_dev,
		sizeof(struct qat_pmd_private), crypto_qat_dev_init);
}

static int crypto_qat_pci_remove(struct rte_pci_device *pci_dev)
{
	return rte_cryptodev_pci_generic_remove(pci_dev, NULL);
}

static struct rte_pci_driver rte_qat_pmd = {
	.id_table = pci_id_qat_map,
	.drv_flags = RTE_PCI_DRV_NEED_MAPPING,
	.probe = crypto_qat_pci_probe,
	.remove = crypto_qat_pci_remove
};

static struct cryptodev_driver qat_crypto_drv;

RTE_PMD_REGISTER_PCI(CRYPTODEV_NAME_QAT_SYM_PMD, rte_qat_pmd);
RTE_PMD_REGISTER_PCI_TABLE(CRYPTODEV_NAME_QAT_SYM_PMD, pci_id_qat_map);
RTE_PMD_REGISTER_CRYPTO_DRIVER(qat_crypto_drv, rte_qat_pmd,
		cryptodev_qat_driver_id);
