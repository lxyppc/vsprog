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

#define MIC2826_CHANNEL_DCDC				0
#define MIC2826_CHANNEL_LDO1				1
#define MIC2826_CHANNEL_LDO2				2
#define MIC2826_CHANNEL_LDO3				3

struct mic2826_t
{
	RESULT (*init)(uint16_t kHz);
	RESULT (*fini)(void);
	RESULT (*config)(uint16_t DCDC_mV, uint16_t LDO1_mV, 
						uint16_t LDO2_mV, uint16_t LDO3_mV);
};
extern const struct mic2826_t mic2826;
