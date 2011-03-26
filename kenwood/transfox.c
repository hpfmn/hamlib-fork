/*
 *  Hamlib backend - SigFox Transfox description
 *  Copyright (c) 2011 by Stephane Fillod
 *
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 *  See the file 'COPYING.LIB' in the main Hamlib distribution directory for
 *  the complete text of the GNU Lesser Public License version 2.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <hamlib/rig.h>
#include "kenwood.h"


#define TRANSFOX_MODES (RIG_MODE_USB)   /* SDR */

#define TRANSFOX_FUNC_ALL (RIG_FUNC_NONE)

#define TRANSFOX_LEVEL_ALL (RIG_LEVEL_ATT|RIG_LEVEL_PREAMP)

#define TRANSFOX_VFO (RIG_VFO_A)
#define TRANSFOX_VFO_OP (RIG_OP_NONE)

#define TRANSFOX_ANTS (RIG_ANT_1)


/* kenwood_transaction() will add this to command strings
 * sent to the rig and remove it from strings returned from
 * the rig, so no need to append ';' manually to command strings.
 */
static struct kenwood_priv_caps transfox_priv_caps  = {
	.cmdtrm =  EOM_KEN,
};

/* TRANSFOX specific rig_caps API function declarations */
static int transfox_open(RIG *rig);
static const char* transfox_get_info(RIG *rig);
static int transfox_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt);
static int transfox_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val);
static int transfox_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val);

/*
 * Transfox rig capabilities.
 * This SDR only share some basic Kenwood's protocol.
 *
 * Part of info comes from http://www.sigfox-system.com/TransFox-FE?lang=en
 */
const struct rig_caps transfox_caps = {
	.rig_model =		RIG_MODEL_TRANSFOX,
	.model_name =		"Transfox",
	.mfg_name =		"SigFox",
	.version =		"20110322",
	.copyright =		"LGPL",
	.status =		RIG_STATUS_UNTESTED,
	.rig_type =		RIG_TYPE_TUNER,
	.ptt_type =		RIG_PTT_RIG,
	.dcd_type =		RIG_DCD_NONE,
	.port_type =		RIG_PORT_SERIAL,
	.serial_rate_min =	9600,
	.serial_rate_max =	19200,
	.serial_data_bits =	8,
	.serial_stop_bits =	1,
	.serial_parity =	RIG_PARITY_NONE,
	.serial_handshake =	RIG_HANDSHAKE_NONE,
	.write_delay =		0,	/* Timing between bytes */
	.post_write_delay =	10,	/* Timing between command strings */
	.timeout =		500,
	.retry =		3,

	.has_get_func =		TRANSFOX_FUNC_ALL,
	.has_set_func =		TRANSFOX_FUNC_ALL,
	.has_get_level =	TRANSFOX_LEVEL_ALL,
	.has_set_level =	RIG_LEVEL_SET(TRANSFOX_LEVEL_ALL),
	.has_get_parm =		RIG_PARM_NONE,
	.has_set_parm =		RIG_PARM_NONE,
	.level_gran =		{},
	.parm_gran =		{},
	.extparms =		NULL,
	.preamp =		{ 22, 44, RIG_DBLST_END, },
	.attenuator =		{ 10, 20, RIG_DBLST_END, },
	.max_rit =		Hz(0),
	.max_xit =		Hz(0),
	.max_ifshift =		Hz(0),
	.vfo_ops =		TRANSFOX_VFO_OP,
	.targetable_vfo =	RIG_TARGETABLE_FREQ,
	.transceive =		RIG_TRN_RIG,
	.bank_qty =		0,
	.chan_desc_sz =		0,

	.chan_list =		{ RIG_CHAN_END },

	.rx_range_list1 =  {
		{MHz(1),MHz(1450),TRANSFOX_MODES,-1,-1,TRANSFOX_VFO,TRANSFOX_ANTS},
		RIG_FRNG_END,
	}, /* rx range */
	.tx_range_list1 =  {
		{MHz(1),MHz(1450),TRANSFOX_MODES,mW(100),mW(100),TRANSFOX_VFO,TRANSFOX_ANTS},
		RIG_FRNG_END,
	}, /* tx range */

	.rx_range_list2 =  {
		{MHz(1),MHz(1450),TRANSFOX_MODES,-1,-1,TRANSFOX_VFO,TRANSFOX_ANTS},
		RIG_FRNG_END,
	}, /* rx range */
	.tx_range_list2 =  {
		{MHz(1),MHz(1450),TRANSFOX_MODES,mW(100),mW(100),TRANSFOX_VFO,TRANSFOX_ANTS},
		RIG_FRNG_END,
	}, /* tx range */
	.tuning_steps =  {
		{TRANSFOX_MODES,1},
		RIG_TS_END,
		},

	/* mode/filter list, remember: order matters! */
	.filters =  {
		{TRANSFOX_MODES, kHz(192)},
		RIG_FLT_END,
	},
	.priv =  (void *)&transfox_priv_caps,

	.rig_init =		kenwood_init,
	.rig_cleanup =		kenwood_cleanup,
	.rig_open =		transfox_open,
	.set_freq =		kenwood_set_freq,
	.get_freq =		kenwood_get_freq,
	.set_level =	transfox_set_level,
	.get_level = 	transfox_get_level,
	.set_ptt =		kenwood_set_ptt,
	.get_ptt =		transfox_get_ptt,
	.get_info =		transfox_get_info,
#if 0
	.set_trn =		transfox_set_trn,
	.get_trn =		transfox_get_trn,
	.scan =		transfox_scan,
	.set_conf =		transfox_set_conf,
	.get_conf =		transfox_get_conf,
#endif

};


/*
 * TRANSFOX extension function definitions follow
 */

/* transfox_open()
 *
 */
int transfox_open(RIG *rig)
{
	rig_debug(RIG_DEBUG_TRACE, "%s called\n", __func__);

	return kenwood_open(rig);
}

int transfox_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
	char buf[8];
	int retval;

	rig_debug(RIG_DEBUG_TRACE, "%s called\n", __func__);

	retval = kenwood_safe_transaction(rig, "Cs", buf, 8, 2);
	if (retval != RIG_OK)
		return retval;

	*ptt = buf[0] == 'T' ? RIG_PTT_ON : RIG_PTT_OFF;

	return RIG_OK;
}

const char* transfox_get_info(RIG *rig)
{
	static char firmbuf[32];
    size_t firmlen = sizeof firmbuf;
	int retval;

	rig_debug(RIG_DEBUG_TRACE, "%s called\n", __func__);

    firmbuf[0] = '\0';

	retval = kenwood_transaction(rig, "CS", 2, firmbuf, &firmlen);
	if (retval != RIG_OK)
		return NULL;

    return firmbuf;
}

int transfox_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
	int ret = RIG_OK;

	rig_debug(RIG_DEBUG_TRACE, "%s called\n", __func__);

	switch (level) {

	case RIG_LEVEL_ATT:
		if (val.i == 0) {
			ret = kenwood_simple_cmd(rig, "C30");
			if (ret != RIG_OK)
				return ret;
			ret = kenwood_simple_cmd(rig, "C20");
			if (ret != RIG_OK)
				return ret;
        } else if (val.i == 10) {
				ret = kenwood_simple_cmd(rig, "C30");
				if (ret != RIG_OK)
					return ret;
				ret = kenwood_simple_cmd(rig, "C21");
				if (ret != RIG_OK)
					return ret;
        } else if (val.i == 20) {
				ret = kenwood_simple_cmd(rig, "C31");
				if (ret != RIG_OK)
					return ret;
				ret = kenwood_simple_cmd(rig, "C21");
				if (ret != RIG_OK)
					return ret;
		}
		break;

	case RIG_LEVEL_PREAMP:
		if (val.i == 0) {
			ret = kenwood_simple_cmd(rig, "C30");
			if (ret != RIG_OK)
				return ret;
			ret = kenwood_simple_cmd(rig, "C20");
			if (ret != RIG_OK)
				return ret;
        } else if (val.i == 22) {
				ret = kenwood_simple_cmd(rig, "C30");
				if (ret != RIG_OK)
					return ret;
				ret = kenwood_simple_cmd(rig, "C22");
				if (ret != RIG_OK)
					return ret;
        } else if (val.i == 44) {
				ret = kenwood_simple_cmd(rig, "C32");
				if (ret != RIG_OK)
					return ret;
				ret = kenwood_simple_cmd(rig, "C22");
				if (ret != RIG_OK)
					return ret;
		}
		break;

	default:
		rig_debug(RIG_DEBUG_ERR, "Unsupported set_level %d", level);
		return -RIG_EINVAL;
	}

	return ret;
}

int transfox_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
	char lvlbuf[16];
	int retval;

	rig_debug(RIG_DEBUG_TRACE, "%s called\n", __func__);

	switch (level) {
	case RIG_LEVEL_ATT:
		retval = kenwood_safe_transaction(rig, "C2x", lvlbuf, 8, 3);
		if (retval != RIG_OK)
			return retval;

		val->i = (lvlbuf[2] == '1') ? 10 : 0;

		retval = kenwood_safe_transaction(rig, "C3x", lvlbuf, 8, 3);
		if (retval != RIG_OK)
			return retval;

		val->i += (lvlbuf[2] == '1') ? 10 : 0;

		break;

	case RIG_LEVEL_PREAMP:

		retval = kenwood_safe_transaction(rig, "C2x", lvlbuf, 8, 3);
		if (retval != RIG_OK)
			return retval;

		val->i = (lvlbuf[2] == '2') ? 22 : 0;

		retval = kenwood_safe_transaction(rig, "C3x", lvlbuf, 8, 3);
		if (retval != RIG_OK)
			return retval;

		val->i += (lvlbuf[2] == '2') ? 22 : 0;

		break;

	default:
		rig_debug(RIG_DEBUG_ERR, "Unsupported get_level %x", level);
		return -RIG_EINVAL;
	}

	return RIG_OK;
}

