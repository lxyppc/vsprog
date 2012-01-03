/***************************************************************************
 *   Copyright (C) 2009 - 2010 by Simon Qian <SimonQian@SimonQian.com>     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "port.h"
#include "app_cfg.h"
#include "app_type.h"
#include "app_io.h"
#include "app_err.h"
#include "app_log.h"

#include "vsprog.h"
#include "programmer.h"
#include "target.h"
#include "scripts.h"

#include "pgbar.h"

#include "cm3.h"
#include "cm3_stm32f1.h"

#include "adi_v5p1.h"
#include "cm3_common.h"

#include "cm3_internal.h"
#include "stm32f1_internal.h"
#include "cm3_stm32_fl.h"

#define	STM32F1_FL_PAGE0_ADDR		\
	(param->chip_areas[SRAM_IDX].addr + STM32_FL_BUFFER_OFFSET)

ENTER_PROGRAM_MODE_HANDLER(stm32f1swj);
LEAVE_PROGRAM_MODE_HANDLER(stm32f1swj);
ERASE_TARGET_HANDLER(stm32f1swj);
WRITE_TARGET_HANDLER(stm32f1swj);
READ_TARGET_HANDLER(stm32f1swj);
const struct program_functions_t stm32f1swj_program_functions =
{
	NULL,
	ENTER_PROGRAM_MODE_FUNCNAME(stm32f1swj),
	LEAVE_PROGRAM_MODE_FUNCNAME(stm32f1swj),
	ERASE_TARGET_FUNCNAME(stm32f1swj),
	WRITE_TARGET_FUNCNAME(stm32f1swj),
	READ_TARGET_FUNCNAME(stm32f1swj)
};

ENTER_PROGRAM_MODE_HANDLER(stm32f1swj)
{
	struct chip_param_t *param = context->param;
	uint32_t reg, flash_obr, flash_wrpr;
	
	// unlock flash and option bytes
	reg = STM32F1_FLASH_UNLOCK_KEY1;
	adi_memap_write_reg32(STM32F1_FLASH_KEYR, &reg, 0);
	reg = STM32F1_FLASH_UNLOCK_KEY2;
	adi_memap_write_reg32(STM32F1_FLASH_KEYR, &reg, 0);
	reg = STM32F1_OPT_UNLOCK_KEY1;
	adi_memap_write_reg32(STM32F1_FLASH_OPTKEYR, &reg, 0);
	reg = STM32F1_OPT_UNLOCK_KEY2;
	adi_memap_write_reg32(STM32F1_FLASH_OPTKEYR, &reg, 0);
	
	adi_memap_read_reg32(STM32F1_FLASH_WRPR, &flash_wrpr, 0);
	if (adi_memap_read_reg32(STM32F1_FLASH_OBR, &flash_obr, 1))
	{
		return VSFERR_FAIL;
	}
	LOG_INFO(INFOMSG_REG_08X, "FLASH_OBR", flash_obr);
	LOG_INFO(INFOMSG_REG_08X, "FLASH_WRPR", flash_wrpr);
	
	if ((flash_obr & STM32F1_FLASH_OBR_RDPRT) || (flash_wrpr != 0xFFFFFFFF))
	{
		LOG_WARNING("STM32 locked, to unlock, run "
					"vsprog -cstm32f1_XX -mX -oeu -owu -tu0xFFFFFFFFFFFFFFA5");
	}
	
	return stm32swj_fl_init(param->chip_areas[SRAM_IDX].addr);
}

LEAVE_PROGRAM_MODE_HANDLER(stm32f1swj)
{
	struct stm32_fl_result_t result;
	REFERENCE_PARAMETER(context);
	REFERENCE_PARAMETER(success);
	
	return stm32swj_fl_wait_ready(&result, true);
}

ERASE_TARGET_HANDLER(stm32f1swj)
{
	struct chip_param_t *param = context->param;
	struct program_info_t *pi = context->pi;
	struct operation_t *op = context->op;
	vsf_err_t err = VSFERR_NONE;
	struct stm32_fl_cmd_t cmd;
	struct stm32_fl_result_t result;
	
	REFERENCE_PARAMETER(size);
	REFERENCE_PARAMETER(addr);
	
	cmd.sr_busy_mask = STM32F1_FLASH_SR_BSY;
	cmd.sr_err_mask = STM32F1_FLASH_SR_ERRMSK;
	switch (area)
	{
	case FUSE_CHAR:
		cmd.cr_addr = STM32F1_FLASH_CR;
		cmd.sr_addr = STM32F1_FLASH_SR;
		cmd.cr_value1 = STM32F1_FLASH_CR_OPTER | STM32F1_FLASH_CR_OPTWRE;
		cmd.cr_value2 = cmd.cr_value1 | STM32F1_FLASH_CR_STRT;
		cmd.data_type = 0;
		cmd.data_size = 1;
		if (stm32swj_fl_call(&cmd, &result, true))
		{
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		
		// if fuse write will not be performed,
		// we MUST write a default non-lock value(0xFFFFFFFFFFFFFFA5) to fuse,
		// or STM32 will be locked
		if (!(op->write_operations & FUSE))
		{
			uint64_t fuse = 0xFFFFFFFFFFFFFFA5;
			
			// TODO: fix here for big-endian
			memcpy(pi->program_areas[FUSE_IDX].buff, &fuse, 8);
			op->write_operations |= FUSE;
		}
		break;
	case APPLICATION_CHAR:
		cmd.cr_addr = STM32F1_FLASH_CR;
		cmd.sr_addr = STM32F1_FLASH_SR;
		cmd.cr_value1 = STM32F1_FLASH_CR_MER;
		cmd.cr_value2 = cmd.cr_value1 | STM32F1_FLASH_CR_STRT;
		cmd.data_type = 0;
		cmd.data_size = 1;
		if (stm32swj_fl_call(&cmd, &result, true))
		{
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		
		if (param->chip_areas[APPLICATION_IDX].size > STM32F1_FLASH_BANK_SIZE)
		{
			cmd.cr_addr = STM32F1_FLASH_CR2;
			cmd.sr_addr = STM32F1_FLASH_SR2;
			if (stm32swj_fl_call(&cmd, &result, true))
			{
				err = ERRCODE_FAILURE_OPERATION;
				break;
			}
		}
		break;
	default:
		err = VSFERR_FAIL;
		break;
	}
	return err;
}

WRITE_TARGET_HANDLER(stm32f1swj)
{
	struct chip_param_t *param = context->param;
	uint8_t i;
	uint8_t fuse_buff[STM32F1_OB_SIZE];
	static uint8_t tick_tock = 0;
	vsf_err_t err = VSFERR_NONE;
	struct stm32_fl_cmd_t cmd;
	struct stm32_fl_result_t result;
	
	cmd.sr_busy_mask = STM32F1_FLASH_SR_BSY;
	cmd.sr_err_mask = STM32F1_FLASH_SR_ERRMSK;
	switch (area)
	{
	case FUSE_CHAR:
		if (size != STM32F1_OB_SIZE / 2)
		{
			return VSFERR_FAIL;
		}
		
		for (i = 0; i < STM32F1_OB_SIZE / 2; i++)
		{
			fuse_buff[2 * i] = buff[i];
			fuse_buff[2 * i + 1] = ~buff[i];
		}
		
		cmd.cr_addr = STM32F1_FLASH_CR;
		cmd.sr_addr = STM32F1_FLASH_SR;
		cmd.cr_value1 = STM32F1_FLASH_CR_OPTPG | STM32F1_FLASH_CR_OPTWRE;
		cmd.cr_value2 = 0;
		cmd.target_addr = STM32F1_OB_ADDR;
		cmd.ram_addr = STM32F1_FL_PAGE0_ADDR;
		cmd.data_type = 2;
		cmd.data_size = STM32F1_OB_SIZE / 2;
		if (adi_memap_write_buf(cmd.ram_addr, fuse_buff, STM32F1_OB_SIZE) ||
			stm32swj_fl_call(&cmd, &result, true))
		{
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		break;
	case APPLICATION_CHAR:
		if (size != param->chip_areas[APPLICATION_IDX].page_size)
		{
			return VSFERR_FAIL;
		}
		
		if (addr >=
			(param->chip_areas[APPLICATION_IDX].addr + STM32F1_FLASH_BANK_SIZE))
		{
			cmd.cr_addr = STM32F1_FLASH_CR2;
			cmd.sr_addr = STM32F1_FLASH_SR2;
		}
		else
		{
			cmd.cr_addr = STM32F1_FLASH_CR;
			cmd.sr_addr = STM32F1_FLASH_SR;
		}
		cmd.cr_value1 = STM32F1_FLASH_CR_PG;
		cmd.cr_value2 = 0;
		cmd.target_addr = addr;
		if (tick_tock & 1)
		{
			cmd.ram_addr = STM32F1_FL_PAGE0_ADDR + size;
		}
		else
		{
			cmd.ram_addr = STM32F1_FL_PAGE0_ADDR;
		}
		cmd.data_type = 2;
		cmd.data_size = size / 2;
		if (adi_memap_write_buf(cmd.ram_addr, buff, size) ||
			stm32swj_fl_call(&cmd, &result, false))
		{
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		tick_tock++;
		break;
	default:
		err = VSFERR_FAIL;
		break;
	}
	
	return err;
}

READ_TARGET_HANDLER(stm32f1swj)
{
	struct program_info_t *pi = context->pi;
	uint8_t option_bytes[STM32F1_OB_SIZE], i;
	uint32_t mcu_id = 0, flash_sram_size, flash_obr;
	uint32_t cur_block_size;
	uint16_t flash_size;
	vsf_err_t err = VSFERR_NONE;
	
	switch (area)
	{
	case CHIPID_CHAR:
		// read MCU ID at STM32F1_REG_MCU_ID
		if (adi_memap_read_reg32(STM32F1_REG_MCU_ID, &mcu_id, 1))
		{
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		mcu_id = LE_TO_SYS_U32(mcu_id);
		stm32f1_print_device(mcu_id);
		mcu_id &= STM32F1_DEN_MSK;
		*(uint32_t *)buff = mcu_id;
		
		if (adi_memap_read_reg32(STM32F1_FLASH_OBR, &flash_obr, 1))
		{
			return VSFERR_FAIL;
		}
		if (flash_obr & STM32F1_FLASH_OBR_RDPRT)
		{
			// read protected, flash size and sram size is not readable
			return VSFERR_NONE;
		}
		
		// read flash and ram size
		if (adi_memap_read_reg32(STM32F1_REG_FLASH_RAM_SIZE, &flash_sram_size, 1))
		{
			LOG_ERROR(ERRMSG_FAILURE_OPERATION, "read stm32f1 flash_ram size");
			err = ERRCODE_FAILURE_OPERATION;
			break;
		}
		flash_sram_size = LE_TO_SYS_U32(flash_sram_size);
		flash_size = stm32f1_get_flash_size(mcu_id, flash_sram_size);
		pi->program_areas[APPLICATION_IDX].size = flash_size * 1024;
		
		LOG_INFO("Flash memory size: %i KB", flash_size);
		if ((flash_sram_size >> 16) != 0xFFFF)
		{
			LOG_INFO("SRAM memory size: %i KB", flash_sram_size >> 16);
		}
		break;
	case FUSE_CHAR:
		if (adi_memap_read_buf(STM32F1_OB_ADDR, option_bytes, STM32F1_OB_SIZE))
		{
			return VSFERR_FAIL;
		}
		for (i = 0; i < size; i++)
		{
			buff[i] = option_bytes[i * 2];
		}
		break;
	case APPLICATION_CHAR:
		while (size)
		{
			// cm3_get_max_block_size return size in dword(4-byte)
			cur_block_size = cm3_get_max_block_size(addr);
			if (cur_block_size > (size >> 2))
			{
				cur_block_size = size;
			}
			else
			{
				cur_block_size <<= 2;
			}
			if (adi_memap_read_buf(addr, buff, cur_block_size))
			{
				LOG_ERROR(ERRMSG_FAILURE_OPERATION_ADDR, "write flash block",
							addr);
				err = ERRCODE_FAILURE_OPERATION_ADDR;
				break;
			}
			
			size -= cur_block_size;
			addr += cur_block_size;
			buff += cur_block_size;
			pgbar_update(cur_block_size);
		}
		break;
	case UNIQUEID_CHAR:
		if (adi_memap_read_reg32(STM32F1_UID_ADDR + 0,
									(((uint32_t *)buff) + 0), 0) ||
			adi_memap_read_reg32(STM32F1_UID_ADDR + 4,
									(((uint32_t *)buff) + 1), 0) ||
			adi_memap_read_reg32(STM32F1_UID_ADDR + 8,
									(((uint32_t *)buff) + 2), 1))
		{
			err = VSFERR_FAIL;
		}
		break;
	default:
		err = VSFERR_FAIL;
		break;
	}
	return err;
}

