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

#include <string.h>
#include <stdlib.h>

#include "app_cfg.h"
#include "app_type.h"
#include "app_io.h"
#include "app_err.h"
#include "app_log.h"

#include "pgbar.h"

#include "vsprog.h"
#include "programmer.h"
#include "target.h"
#include "scripts.h"
#include "app_scripts.h"

#include "dal/dal.h"
#include "dal/mal/mal.h"
#include "dal/ee24cxx/ee24cxx_drv.h"

#include "ee24cxx.h"
#include "ee24cxx_internal.h"

#define CUR_TARGET_STRING			EE24CXX_STRING

struct program_area_map_t ee24cxx_program_area_map[] = 
{
	{EEPROM_CHAR, 1, 0, 0, 0, AREA_ATTR_EWR | AREA_ATTR_EP},
	{0, 0, 0, 0, 0, 0}
};

const struct program_mode_t ee24cxx_program_mode[] = 
{
	{'*', SET_FREQUENCY, IFS_I2C},
	{0, NULL, 0}
};

ENTER_PROGRAM_MODE_HANDLER(ee24cxx);
LEAVE_PROGRAM_MODE_HANDLER(ee24cxx);
ERASE_TARGET_HANDLER(ee24cxx);
WRITE_TARGET_HANDLER(ee24cxx);
READ_TARGET_HANDLER(ee24cxx);
const struct program_functions_t ee24cxx_program_functions = 
{
	NULL,			// execute
	ENTER_PROGRAM_MODE_FUNCNAME(ee24cxx), 
	LEAVE_PROGRAM_MODE_FUNCNAME(ee24cxx), 
	ERASE_TARGET_FUNCNAME(ee24cxx), 
	WRITE_TARGET_FUNCNAME(ee24cxx), 
	READ_TARGET_FUNCNAME(ee24cxx)
};

VSS_HANDLER(ee24cxx_help)
{
	VSS_CHECK_ARGC(1);
	PRINTF("\
Usage of %s:\n\
  -F,  --frequency <FREQUENCY>              set IIC frequency, in KHz\n\n", 
			CUR_TARGET_STRING);
	return ERROR_OK;
}

const struct vss_cmd_t ee24cxx_notifier[] = 
{
	VSS_CMD(	"help",
				"print help information of current target for internal call",
				ee24cxx_help),
	VSS_CMD_END
};






static struct ee24cxx_drv_param_t ee24cxx_drv_param;
static struct ee24cxx_drv_interface_t ee24cxx_drv_ifs;
static struct mal_info_t ee24cxx_mal_info = 
{
	{0, 0}, NULL
};
static struct dal_info_t ee24cxx_dal_info = 
{
	&ee24cxx_drv_ifs, 
	&ee24cxx_drv_param, 
	NULL,
	&ee24cxx_mal_info,
};

ENTER_PROGRAM_MODE_HANDLER(ee24cxx)
{
	struct chip_param_t *param = context->param;
	struct program_info_t *pi = context->pi;
	
	if (ERROR_OK != dal_init(context->prog))
	{
		return ERROR_FAIL;
	}
	
	if (pi->ifs_indexes != NULL)
	{
		if (ERROR_OK != dal_config_interface(EE24CXX_STRING, pi->ifs_indexes, 
												&ee24cxx_dal_info))
		{
			return ERROR_FAIL;
		}
	}
	else
	{
		ee24cxx_drv_ifs.iic_port = 0;
	}
	
	ee24cxx_drv_param.iic_addr = 0xAC;
	ee24cxx_drv_param.iic_khz = pi->frequency;
	if (ERROR_OK != mal.init(MAL_IDX_EE24CXX, &ee24cxx_dal_info))
	{
		return ERROR_FAIL;
	}
	ee24cxx_mal_info.capacity.block_size = 
										param->chip_areas[EEPROM_IDX].page_size;
	ee24cxx_mal_info.capacity.block_number = 
										param->chip_areas[EEPROM_IDX].page_num;
	
	return dal_commit();
}

LEAVE_PROGRAM_MODE_HANDLER(ee24cxx)
{
	REFERENCE_PARAMETER(context);
	REFERENCE_PARAMETER(success);
	
	mal.fini(MAL_IDX_EE24CXX, &ee24cxx_dal_info);
	return dal_commit();
}

ERASE_TARGET_HANDLER(ee24cxx)
{
	REFERENCE_PARAMETER(context);
	REFERENCE_PARAMETER(area);
	REFERENCE_PARAMETER(addr);
	REFERENCE_PARAMETER(size);
	
	// no need to erase
	return ERROR_OK;
}

WRITE_TARGET_HANDLER(ee24cxx)
{
	struct chip_param_t *param = context->param;
	
	switch (area)
	{
	case EEPROM_CHAR:
		if (size % param->chip_areas[EEPROM_IDX].page_size)
		{
			return ERROR_FAIL;
		}
		size /= param->chip_areas[EEPROM_IDX].page_size;
		
		if (ERROR_OK != mal.writeblock(MAL_IDX_EE24CXX, &ee24cxx_dal_info, 
										addr, buff, size))
		{
			return ERROR_FAIL;
		}
		return dal_commit();
		break;
	default:
		return ERROR_FAIL;
	}
}

READ_TARGET_HANDLER(ee24cxx)
{
	struct chip_param_t *param = context->param;
	
	switch (area)
	{
	case CHIPID_CHAR:
		return ERROR_OK;
		break;
	case EEPROM_CHAR:
		if (size % param->chip_areas[EEPROM_IDX].page_size)
		{
			return ERROR_FAIL;
		}
		size /= param->chip_areas[EEPROM_IDX].page_size;
		
		if (ERROR_OK != mal.readblock(MAL_IDX_EE24CXX, &ee24cxx_dal_info, 
										addr, buff, size))
		{
			return ERROR_FAIL;
		}
		return dal_commit();
		break;
	default:
		return ERROR_FAIL;
	}
}
