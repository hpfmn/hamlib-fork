/*
 *	Hamlib Dummy backend - main file
 *	Copyright (c) 2001-2010 by Stephane Fillod
 *	Copyright (c) 2010 by Nate Bargmann
 *
 *
 *	 This library is free software; you can redistribute it and/or
 *	 modify it under the terms of the GNU Lesser General Public
 *	 License as published by the Free Software Foundation; either
 *	 version 2.1 of the License, or (at your option) any later version.
 *
 *	 This library is distributed in the hope that it will be useful,
 *	 but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 *	 Lesser General Public License for more details.
 *
 *	 You should have received a copy of the GNU Lesser General Public
 *	 License along with this library; if not, write to the Free Software
 *	 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301	USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* String function definitions */
#include <unistd.h>	/* UNIX standard function definitions */
#include <math.h>
#include <time.h>

#include "hamlib/rig.h"
#include "serial.h"
#include "parallel.h"
#include "misc.h"
#include "tones.h"
#include "idx_builtin.h"
#include "register.h"

#include "satnogs.h"

#define NB_CHAN 22		/* see caps->chan_list */

struct satnogs_priv_data {
		/* current vfo already in rig_state ? */
		vfo_t curr_vfo;
		vfo_t last_vfo;	/* VFO A or VFO B, when in MEM mode */

		ptt_t ptt;
		powerstat_t powerstat;
		int bank;
		value_t parms[RIG_SETTING_MAX];

		channel_t *curr;	/* points to vfo_a, vfo_b or mem[] */

		channel_t vfo_a;
		channel_t vfo_b;
		channel_t mem[NB_CHAN];

		struct ext_list *ext_parms;

		char *magic_conf;
		int static_data;
};

/* levels pertain to each VFO */
static const struct confparams satnogs_ext_levels[] = {
	{ TOK_EL_MAGICLEVEL, "MGL", "Magic level", "Magic level, as an example",
		NULL, RIG_CONF_NUMERIC, { .n = { 0, 1, .001 } }
	},
	{ TOK_EL_MAGICFUNC, "MGF", "Magic func", "Magic function, as an example",
		NULL, RIG_CONF_CHECKBUTTON
			 	},
	{ TOK_EL_MAGICOP, "MGO", "Magic Op", "Magic Op, as an example",
		NULL, RIG_CONF_BUTTON
			 	},
	{ TOK_EL_MAGICOP, "MGS", "Magic String", "Magic String, as an example",
		NULL, RIG_CONF_STRING
			 	},
	{ RIG_CONF_END, NULL, }
};

/* parms pertain to the whole rig */
static const struct confparams satnogs_ext_parms[] = {
	{ TOK_EP_MAGICPARM, "MGP", "Magic parm", "Magic parameter, as an example",
		NULL, RIG_CONF_NUMERIC, { .n = { 0, 1, .001 } }
	},
	{ RIG_CONF_END, NULL, }
};

/* cfgparams are configuration item generally used by the backend's open() method */
static const struct confparams satnogs_cfg_params[] = {
	{ TOK_CFG_MAGICCONF, "mcfg", "Magic conf", "Magic parameter, as an example",
		"DX", RIG_CONF_STRING, { }
	},
	{ TOK_CFG_STATIC_DATA, "static_data", "Static data", "Output only static data, no randomization of S-meter values",
		"0", RIG_CONF_CHECKBUTTON, { }
	},
	{ RIG_CONF_END, NULL, }
};

/********************************************************************/

static void init_chan(RIG *rig, vfo_t vfo, channel_t *chan)
{
	chan->channel_num = 0;
	chan->vfo = vfo;
	strcpy(chan->channel_desc, rig_strvfo(vfo));

	chan->freq = MHz(145);
	chan->mode = RIG_MODE_FM;
	chan->width = rig_passband_normal(rig, RIG_MODE_FM);
	chan->tx_freq = chan->freq;
	chan->tx_mode = chan->mode;
	chan->tx_width = chan->width;
	chan->split = RIG_SPLIT_OFF;

	chan->rptr_shift = RIG_RPT_SHIFT_NONE;
	chan->rptr_offs = 0;
	chan->ctcss_tone = 0;
	chan->dcs_code = 0;
	chan->ctcss_sql = 0;
	chan->dcs_sql = 0;
	chan->rit = 0;
	chan->xit = 0;
	chan->tuning_step = 0;
	chan->ant = 0;

	chan->funcs = (setting_t)0;
	memset(chan->levels, 0, RIG_SETTING_MAX*sizeof(value_t));
}

static void copy_chan(channel_t *dest, const channel_t *src)
{
		struct ext_list *saved_ext_levels;
		int i;

		/* TODO: ext_levels[] of different sizes */

		for (i=0; !RIG_IS_EXT_END(src->ext_levels[i]) &&
						!RIG_IS_EXT_END(dest->ext_levels[i]); i++) {
				dest->ext_levels[i] = src->ext_levels[i];
		}

		saved_ext_levels = dest->ext_levels;
		memcpy(dest, src, sizeof(channel_t));
		dest->ext_levels = saved_ext_levels;
}

static struct ext_list * alloc_init_ext(const struct confparams *cfp)
{
	struct ext_list *elp;
	int i, nb_ext;

	for (nb_ext=0; !RIG_IS_EXT_END(cfp[nb_ext]); nb_ext++)
		;

	elp = calloc((nb_ext+1), sizeof(struct ext_list));
	if (!elp)
	return NULL;

	for (i=0; !RIG_IS_EXT_END(cfp[i]); i++)
	{
		elp[i].token = cfp[i].token;
		/* value reset already by calloc */
	}
	/* last token in array is set to 0 by calloc */

	return elp;
}

static struct ext_list * find_ext(struct ext_list *elp, token_t token)
{
	int i;

	for (i=0; elp[i].token != 0; i++)
	{
		if (elp[i].token == token)
			return &elp[i];
	}

	return NULL;
}

static int satnogs_init(RIG *rig)
{
	struct satnogs_priv_data *priv;
	int i;

	priv = (struct satnogs_priv_data*)malloc(sizeof(struct satnogs_priv_data));
	if (!priv)
			return -RIG_ENOMEM;
	rig->state.priv = (void*)priv;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called\n", __FUNCTION__);
	rig->state.rigport.type.rig = RIG_PORT_NONE;

	priv->ptt = RIG_PTT_OFF;
	priv->powerstat = RIG_POWER_ON;
	priv->bank = 0;
	memset(priv->parms, 0, RIG_SETTING_MAX*sizeof(value_t));

	memset(priv->mem, 0, sizeof(priv->mem));
	for (i=0; i<NB_CHAN; i++) {
	priv->mem[i].channel_num = i;
	priv->mem[i].vfo = RIG_VFO_MEM;

	priv->mem[i].ext_levels = alloc_init_ext(satnogs_ext_levels);
	if (!priv->mem[i].ext_levels)
		return -RIG_ENOMEM;
	}

	priv->vfo_a.ext_levels = alloc_init_ext(satnogs_ext_levels);
	if (!priv->vfo_a.ext_levels)
		return -RIG_ENOMEM;
	priv->vfo_b.ext_levels = alloc_init_ext(satnogs_ext_levels);
	if (!priv->vfo_b.ext_levels)
		return -RIG_ENOMEM;

	priv->ext_parms = alloc_init_ext(satnogs_ext_parms);
	if (!priv->ext_parms)
		return -RIG_ENOMEM;

	init_chan(rig, RIG_VFO_A, &priv->vfo_a);
	init_chan(rig, RIG_VFO_B, &priv->vfo_b);
	priv->curr = &priv->vfo_a;
	priv->curr_vfo = priv->last_vfo = RIG_VFO_A;

	priv->magic_conf = strdup("DX");

	return RIG_OK;
}

static int satnogs_cleanup(RIG *rig)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	int i;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called\n", __FUNCTION__);

	for (i=0; i<NB_CHAN; i++) {
		free(priv->mem[i].ext_levels);
	}
	free(priv->vfo_a.ext_levels);
	free(priv->vfo_b.ext_levels);
	free(priv->ext_parms);
	free(priv->magic_conf);

	if (rig->state.priv)
		free(rig->state.priv);

	rig->state.priv = NULL;

	return RIG_OK;
}

void start_gnuradio();
void stop_gnuradio();

static int satnogs_open(RIG *rig)
{
	rig_debug(RIG_DEBUG_VERBOSE,"%s called\n", __FUNCTION__);
	start_gnuradio();

	return RIG_OK;
}

static int satnogs_close(RIG *rig)
{
	rig_debug(RIG_DEBUG_VERBOSE,"%s called\n", __FUNCTION__);
	stop_gnuradio();

	return RIG_OK;
}

static int satnogs_set_conf(RIG *rig, token_t token, const char *val)
{
	struct satnogs_priv_data *priv;

	priv = (struct satnogs_priv_data*)rig->state.priv;

	switch(token) {
		case TOK_CFG_MAGICCONF:
			if (val) {
				free(priv->magic_conf);
				priv->magic_conf = strdup(val);
			}
			break;
		case TOK_CFG_STATIC_DATA:
			priv->static_data = atoi(val) ? 1 : 0;
			break;
		default:
			return -RIG_EINVAL;
	}
	return RIG_OK;
}

static int satnogs_get_conf(RIG *rig, token_t token, char *val)
{
	struct satnogs_priv_data *priv;

	priv = (struct satnogs_priv_data*)rig->state.priv;

	switch(token) {
		case TOK_CFG_MAGICCONF:
			strcpy(val, priv->magic_conf);
			break;
		default:
			return -RIG_EINVAL;
	}
	return RIG_OK;
}

static int satnogs_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	char fstr[20];

	sprintf_freq(fstr, freq);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n", __FUNCTION__,
 			rig_strvfo(vfo), fstr);
	curr->freq = freq;

	return RIG_OK;
}


static int satnogs_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, rig_strvfo(vfo));

	*freq = curr->freq;

	return RIG_OK;
}

void satnogs_gr_set_mode(const char* mode);

static int satnogs_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	char buf[16];

	sprintf_freq(buf, width);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s %s\n", __FUNCTION__,
			rig_strvfo(vfo), rig_strrmode(mode), buf);

	curr->mode = mode;

	switch(curr->mode) {
		case RIG_MODE_FM:
			satnogs_gr_set_mode("FM");
			break;
	}

	if (RIG_PASSBAND_NOCHANGE == width) return RIG_OK;

	if (width == RIG_PASSBAND_NORMAL)
		curr->width = rig_passband_normal(rig, mode);
	else
		curr->width = width;

	return RIG_OK;
}


static int satnogs_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, rig_strvfo(vfo));

	*mode = curr->mode;
	*width = curr->width;

	return RIG_OK;
}


static int satnogs_set_vfo(RIG *rig, vfo_t vfo)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, rig_strvfo(vfo));

	priv->last_vfo = priv->curr_vfo;
	priv->curr_vfo = vfo;
	switch (vfo) {
	case RIG_VFO_VFO:	/* FIXME */

	case RIG_VFO_A: priv->curr = &priv->vfo_a; break;
	case RIG_VFO_B: priv->curr = &priv->vfo_b; break;
	case RIG_VFO_MEM:
					if (curr->channel_num >= 0 && curr->channel_num < NB_CHAN) {
							priv->curr = &priv->mem[curr->channel_num];
							break;
					}
	default:
		rig_debug(RIG_DEBUG_VERBOSE,"%s unknown vfo: %s\n", __FUNCTION__, rig_strvfo(vfo));
	}

	return RIG_OK;
}


static int satnogs_get_vfo(RIG *rig, vfo_t *vfo)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	*vfo = priv->curr_vfo;
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, rig_strvfo(*vfo));

	return RIG_OK;
}


static int satnogs_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	priv->ptt = ptt;

	return RIG_OK;
}


static int satnogs_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	int status = 0;
	ptt_t par_status = RIG_PTT_OFF;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	*ptt = priv->ptt;

	// sneak a look at the hardware PTT and OR that in with our result
	// as if it had keyed us
	switch (rig->state.pttport.type.ptt)
		{
		case RIG_PTT_SERIAL_DTR: if (rig->state.pttport.fd >= 0) ser_get_dtr (&rig->state.pttport, &status); break;
		case RIG_PTT_SERIAL_RTS: if (rig->state.pttport.fd >= 0) ser_get_rts (&rig->state.pttport, &status); break;
		case RIG_PTT_PARALLEL: if (rig->state.pttport.fd >= 0) par_ptt_get (&rig->state.pttport, &par_status); break;
		default: break;
		}
	*ptt = *ptt || status || RIG_PTT_ON == par_status;
	return RIG_OK;
}


static int satnogs_get_dcd(RIG *rig, vfo_t vfo, dcd_t *dcd)
{
	static int twiddle = 0;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	*dcd = twiddle++ & 1 ? RIG_DCD_ON : RIG_DCD_OFF;

	return RIG_OK;
}


static int satnogs_set_rptr_shift(RIG *rig, vfo_t vfo, rptr_shift_t rptr_shift)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->rptr_shift = rptr_shift;

	return RIG_OK;
}


static int satnogs_get_rptr_shift(RIG *rig, vfo_t vfo, rptr_shift_t *rptr_shift)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*rptr_shift = curr->rptr_shift;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_rptr_offs(RIG *rig, vfo_t vfo, shortfreq_t rptr_offs)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->rptr_offs = rptr_offs;

	return RIG_OK;
}


static int satnogs_get_rptr_offs(RIG *rig, vfo_t vfo, shortfreq_t *rptr_offs)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*rptr_offs = curr->rptr_offs;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_ctcss_tone(RIG *rig, vfo_t vfo, tone_t tone)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->ctcss_tone = tone;

	return RIG_OK;
}


static int satnogs_get_ctcss_tone(RIG *rig, vfo_t vfo, tone_t *tone)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*tone = curr->ctcss_tone;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_dcs_code(RIG *rig, vfo_t vfo, tone_t code)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->dcs_code = code;

	return RIG_OK;
}


static int satnogs_get_dcs_code(RIG *rig, vfo_t vfo, tone_t *code)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*code = curr->dcs_code;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_ctcss_sql(RIG *rig, vfo_t vfo, tone_t tone)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->ctcss_sql = tone;

	return RIG_OK;
}


static int satnogs_get_ctcss_sql(RIG *rig, vfo_t vfo, tone_t *tone)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*tone = curr->ctcss_sql;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_dcs_sql(RIG *rig, vfo_t vfo, unsigned int code)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->dcs_sql = code;

	return RIG_OK;
}


static int satnogs_get_dcs_sql(RIG *rig, vfo_t vfo, unsigned int *code)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*code = curr->dcs_sql;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_split_freq(RIG *rig, vfo_t vfo, freq_t tx_freq)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	char fstr[20];

	sprintf_freq(fstr, tx_freq);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n", __FUNCTION__,
 			rig_strvfo(vfo), fstr);
	curr->tx_freq = tx_freq;

	return RIG_OK;
}


static int satnogs_get_split_freq(RIG *rig, vfo_t vfo, freq_t *tx_freq)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__,rig_strvfo(vfo));

	*tx_freq = curr->tx_freq;

	return RIG_OK;
}

static int satnogs_set_split_mode(RIG *rig, vfo_t vfo, rmode_t tx_mode, pbwidth_t tx_width)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	char buf[16];

	sprintf_freq(buf, tx_width);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s %s\n", __FUNCTION__,
			rig_strvfo(vfo), rig_strrmode(tx_mode), buf);

	curr->tx_mode = tx_mode;
	if (RIG_PASSBAND_NOCHANGE == tx_width) return RIG_OK;

	curr->tx_width = tx_width;

	return RIG_OK;
}

static int satnogs_get_split_mode(RIG *rig, vfo_t vfo, rmode_t *tx_mode, pbwidth_t *tx_width)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, rig_strvfo(vfo));

	*tx_mode = curr->tx_mode;
	*tx_width = curr->tx_width;

	return RIG_OK;
}

static int satnogs_set_split_vfo(RIG *rig, vfo_t vfo, split_t split, vfo_t tx_vfo)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->split = split;

	return RIG_OK;
}


static int satnogs_get_split_vfo(RIG *rig, vfo_t vfo, split_t *split, vfo_t *tx_vfo)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*split = curr->split;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}

static int satnogs_set_rit(RIG *rig, vfo_t vfo, shortfreq_t rit)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->rit = rit;

	return RIG_OK;
}


static int satnogs_get_rit(RIG *rig, vfo_t vfo, shortfreq_t *rit)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*rit = curr->rit;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_xit(RIG *rig, vfo_t vfo, shortfreq_t xit)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->xit = xit;

	return RIG_OK;
}


static int satnogs_get_xit(RIG *rig, vfo_t vfo, shortfreq_t *xit)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*xit = curr->xit;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_ts(RIG *rig, vfo_t vfo, shortfreq_t ts)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	curr->tuning_step = ts;

	return RIG_OK;
}


static int satnogs_get_ts(RIG *rig, vfo_t vfo, shortfreq_t *ts)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*ts = curr->tuning_step;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %d\n",__FUNCTION__,
					rig_strfunc(func), status);
	if (status)
	curr->funcs |=	func;
	else
	curr->funcs &= ~func;

	return RIG_OK;
}


static int satnogs_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*status = curr->funcs & func ? 1 : 0;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n",__FUNCTION__,
					rig_strfunc(func));

	return RIG_OK;
}


static int satnogs_set_level(RIG *rig, vfo_t vfo, setting_t level, value_t val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	int idx;
	char lstr[32];

	idx = rig_setting2idx(level);
	if (idx >= RIG_SETTING_MAX)
			return -RIG_EINVAL;

	curr->levels[idx] = val;

	if (RIG_LEVEL_IS_FLOAT(level))
			sprintf(lstr, "%f", val.f);
	else
			sprintf(lstr, "%d", val.i);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n",__FUNCTION__,
					rig_strlevel(level), lstr);

	return RIG_OK;
}


static int satnogs_get_level(RIG *rig, vfo_t vfo, setting_t level, value_t *val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	int idx;

	idx = rig_setting2idx(level);

	if (idx >= RIG_SETTING_MAX)
			return -RIG_EINVAL;

	if (level == RIG_LEVEL_STRENGTH || level == RIG_LEVEL_RAWSTR) {
		if (priv->static_data) {
			curr->levels[idx].i = -12;
		} else {
			/* make S-Meter jiggle */
			int qrm = -56;
			if (curr->freq < MHz(7))
					qrm = -20;
			else if (curr->freq < MHz(21))
					qrm = -30;
			else if (curr->freq < MHz(50))
					qrm = -50;

			curr->levels[idx].i = qrm + time(NULL)%32 + rand()%4
					- curr->levels[LVL_ATT].i
					+ curr->levels[LVL_PREAMP].i;
		}
	}

	*val = curr->levels[idx];
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n",__FUNCTION__,
					rig_strlevel(level));

	return RIG_OK;
}

static int satnogs_set_ext_level(RIG *rig, vfo_t vfo, token_t token, value_t val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	char lstr[64];
	const struct confparams *cfp;
	struct ext_list *elp;

	cfp = rig_ext_lookup_tok(rig, token);
	if (!cfp)
		return -RIG_EINVAL;

	switch (token) {
		case TOK_EL_MAGICLEVEL:
		case TOK_EL_MAGICFUNC:
		case TOK_EL_MAGICOP:
			break;

		default:
			return -RIG_EINVAL;
	}

	switch (cfp->type) {
		case RIG_CONF_STRING:
			strcpy(lstr, val.s);
			break;
		case RIG_CONF_COMBO:
			sprintf(lstr, "%d", val.i);
			break;
		case RIG_CONF_NUMERIC:
			sprintf(lstr, "%f", val.f);
			break;
		case RIG_CONF_CHECKBUTTON:
			sprintf(lstr, "%s", val.i ? "ON" : "OFF");
			break;
		case RIG_CONF_BUTTON:
			lstr[0] = '\0';
			break;
		default:
			return -RIG_EINTERNAL;
	}

	elp = find_ext(curr->ext_levels, token);
	if (!elp)
			return -RIG_EINTERNAL;
	/* store value */
	elp->val = val;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n",__FUNCTION__,
					cfp->name, lstr);

	return RIG_OK;
}

static int satnogs_get_ext_level(RIG *rig, vfo_t vfo, token_t token, value_t *val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	const struct confparams *cfp;
	struct ext_list *elp;

	cfp = rig_ext_lookup_tok(rig, token);
	if (!cfp)
		return -RIG_EINVAL;

	switch (token) {
		case TOK_EL_MAGICLEVEL:
		case TOK_EL_MAGICFUNC:
		case TOK_EL_MAGICOP:
			break;

		default:
			return -RIG_EINVAL;
	}

	elp = find_ext(curr->ext_levels, token);
	if (!elp)
			return -RIG_EINTERNAL;
	/* load value */
	*val = elp->val;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n",__FUNCTION__,
					cfp->name);

	return RIG_OK;
}


static int satnogs_set_powerstat(RIG *rig, powerstat_t status)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	priv->powerstat = status;

	return RIG_OK;
}


static int satnogs_get_powerstat(RIG *rig, powerstat_t *status)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	*status = priv->powerstat;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_parm(RIG *rig, setting_t parm, value_t val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	int idx;
	char pstr[32];

	idx = rig_setting2idx(parm);
	if (idx >= RIG_SETTING_MAX)
			return -RIG_EINVAL;

	if (RIG_PARM_IS_FLOAT(parm))
			sprintf(pstr, "%f", val.f);
	else
			sprintf(pstr, "%d", val.i);
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n", __FUNCTION__,
					rig_strparm(parm), pstr);
	priv->parms[idx] = val;

	return RIG_OK;
}


static int satnogs_get_parm(RIG *rig, setting_t parm, value_t *val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	int idx;

	idx = rig_setting2idx(parm);
	if (idx >= RIG_SETTING_MAX)
			return -RIG_EINVAL;

	*val = priv->parms[idx];
	rig_debug(RIG_DEBUG_VERBOSE,"%s called %s\n",__FUNCTION__,
					rig_strparm(parm));

	return RIG_OK;
}

static int satnogs_set_ext_parm(RIG *rig, token_t token, value_t val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	char lstr[64];
	const struct confparams *cfp;
	struct ext_list *epp;

	cfp = rig_ext_lookup_tok(rig, token);
	if (!cfp)
		return -RIG_EINVAL;

	switch (token) {
		case TOK_EP_MAGICPARM:
			break;

		default:
			return -RIG_EINVAL;
	}

	switch (cfp->type) {
		case RIG_CONF_STRING:
			strcpy(lstr, val.s);
			break;
		case RIG_CONF_COMBO:
			sprintf(lstr, "%d", val.i);
			break;
		case RIG_CONF_NUMERIC:
			sprintf(lstr, "%f", val.f);
			break;
		case RIG_CONF_CHECKBUTTON:
			sprintf(lstr, "%s", val.i ? "ON" : "OFF");
			break;
		case RIG_CONF_BUTTON:
			lstr[0] = '\0';
			break;
		default:
			return -RIG_EINTERNAL;
	}

	epp = find_ext(priv->ext_parms, token);
	if (!epp)
			return -RIG_EINTERNAL;
	/* store value */
	epp->val = val;


	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %s\n",__FUNCTION__,
					cfp->name, lstr);

	return RIG_OK;
}

static int satnogs_get_ext_parm(RIG *rig, token_t token, value_t *val)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	const struct confparams *cfp;
	struct ext_list *epp;

	/* TODO: load value from priv->ext_parms */

	cfp = rig_ext_lookup_tok(rig, token);
	if (!cfp)
		return -RIG_EINVAL;

	switch (token) {
		case TOK_EP_MAGICPARM:
			break;

		default:
			return -RIG_EINVAL;
	}

	epp = find_ext(priv->ext_parms, token);
	if (!epp)
			return -RIG_EINTERNAL;
	/* load value */
	*val = epp->val;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n",__FUNCTION__,
					cfp->name);

	return RIG_OK;
}



static int satnogs_set_ant(RIG *rig, vfo_t vfo, ant_t ant)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	curr->ant = ant;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_get_ant(RIG *rig, vfo_t vfo, ant_t *ant)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	*ant = curr->ant;

	return RIG_OK;
}


static int satnogs_set_bank(RIG *rig, vfo_t vfo, int bank)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	priv->bank = bank;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_set_mem(RIG *rig, vfo_t vfo, int ch)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	if (ch < 0 || ch >= NB_CHAN)
	return -RIG_EINVAL;

	if (priv->curr_vfo == RIG_VFO_MEM)
			priv->curr = &priv->mem[ch];
	else
			priv->curr->channel_num = ch;

	return RIG_OK;
}


static int satnogs_get_mem(RIG *rig, vfo_t vfo, int *ch)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;

	*ch = curr->channel_num;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}

static int satnogs_scan(RIG *rig, vfo_t vfo, scan_t scan, int ch)
{
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s %d\n",__FUNCTION__,
					rig_strscan(scan), ch);
	/* TODO: change freq, etc. */
	return RIG_OK;
}

static void chan_vfo(channel_t *chan, vfo_t vfo)
{
	chan->vfo = vfo;
	strcpy(chan->channel_desc, rig_strvfo(vfo));
}

static int satnogs_vfo_op(RIG *rig, vfo_t vfo, vfo_op_t op)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;
	channel_t *curr = priv->curr;
	int ret;
	freq_t freq;
	shortfreq_t ts;

	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n",__FUNCTION__,
					rig_strvfop(op));

	switch (op) {
		case RIG_OP_FROM_VFO:	/* VFO->MEM */
				if (priv->curr_vfo == RIG_VFO_MEM) {
				int ch = curr->channel_num;
					copy_chan(curr, priv->last_vfo == RIG_VFO_A ?
								&priv->vfo_a : &priv->vfo_b);
				curr->channel_num = ch;
				curr->channel_desc[0] = '\0';
				curr->vfo = RIG_VFO_MEM;
				} else {
				channel_t *mem_chan = &priv->mem[curr->channel_num];
					copy_chan(mem_chan, curr);
				mem_chan->channel_num = curr->channel_num;
				mem_chan->channel_desc[0] = '\0';
				mem_chan->vfo = RIG_VFO_MEM;
				}
				break;
		case RIG_OP_TO_VFO:				 /* MEM->VFO */
				if (priv->curr_vfo == RIG_VFO_MEM) {
					channel_t *vfo_chan = (priv->last_vfo == RIG_VFO_A) ?
											&priv->vfo_a : &priv->vfo_b;
					copy_chan(vfo_chan, curr);
				chan_vfo(vfo_chan, priv->last_vfo);
				} else {
					copy_chan(&priv->mem[curr->channel_num], curr);
				chan_vfo(curr, priv->curr_vfo);
				}
				break;
		case RIG_OP_CPY:	 /* VFO A = VFO B	 or	 VFO B = VFO A */
				if (priv->curr_vfo == RIG_VFO_A) {
					copy_chan(&priv->vfo_b, &priv->vfo_a);
				chan_vfo(&priv->vfo_b, RIG_VFO_B);
				break;
				} else if (priv->curr_vfo == RIG_VFO_B) {
					copy_chan(&priv->vfo_a, &priv->vfo_b);
				chan_vfo(&priv->vfo_a, RIG_VFO_A);
				break;
				}
					rig_debug(RIG_DEBUG_VERBOSE,"%s beep!\n", __FUNCTION__ );
				break;
		case RIG_OP_XCHG: /* Exchange VFO A/B */
				{
				channel_t chan;
				chan.ext_levels = alloc_init_ext(satnogs_ext_levels);
				if (!chan.ext_levels)
					return -RIG_ENOMEM;
					copy_chan(&chan, &priv->vfo_b);
					copy_chan(&priv->vfo_b, &priv->vfo_a);
					copy_chan(&priv->vfo_a, &chan);
				chan_vfo(&priv->vfo_a, RIG_VFO_A);
				chan_vfo(&priv->vfo_b, RIG_VFO_B);
				free(chan.ext_levels);
				break;
				}
		case RIG_OP_MCL:	/* Memory clear */
				if (priv->curr_vfo == RIG_VFO_MEM) {
					struct ext_list *saved_ext_levels = curr->ext_levels;
					int saved_ch = curr->channel_num;
					int i;

					for (i=0; !RIG_IS_EXT_END(curr->ext_levels[i]); i++) {
						curr->ext_levels[i].val.i = 0;
					}
						memset(curr, 0, sizeof(channel_t));
					curr->ext_levels = saved_ext_levels;
					curr->channel_num = saved_ch;
					curr->vfo = RIG_VFO_MEM;
				} else {
					struct ext_list *saved_ext_levels = curr->ext_levels;
					channel_t *mem_chan = &priv->mem[curr->channel_num];
					int i;

					for (i=0; !RIG_IS_EXT_END(mem_chan->ext_levels[i]); i++) {
						mem_chan->ext_levels[i].val.i = 0;
					}
						memset(mem_chan, 0, sizeof(channel_t));
					mem_chan->ext_levels = saved_ext_levels;
					mem_chan->channel_num = curr->channel_num;
					mem_chan->vfo = RIG_VFO_MEM;
				}
				break;
		case RIG_OP_TOGGLE:
				if (priv->curr_vfo == RIG_VFO_A)
				return satnogs_set_vfo(rig, RIG_VFO_B);
							else if (priv->curr_vfo == RIG_VFO_B)
				return satnogs_set_vfo(rig, RIG_VFO_A);
							else
				return -RIG_EVFO;

		case RIG_OP_RIGHT:
		case RIG_OP_LEFT:
		case RIG_OP_TUNE:
				/* NOP */
				break;
		case RIG_OP_BAND_UP:
		case RIG_OP_BAND_DOWN:
		return -RIG_ENIMPL;

	case RIG_OP_UP:
		ret = satnogs_get_freq(rig, vfo, &freq);
		if (!ret) break;
		ret = satnogs_get_ts(rig, vfo, &ts);
		if (!ret) break;
		ret = satnogs_set_freq(rig, vfo, freq+ts);	/* up */
		break;
	case RIG_OP_DOWN:
		ret = satnogs_get_freq(rig, vfo, &freq);
		if (!ret) break;
		ret = satnogs_get_ts(rig, vfo, &ts);
		if (!ret) break;
		ret = satnogs_set_freq(rig, vfo, freq-ts);	/* down */
		break;

	default:
		break;
	}

	return RIG_OK;
}

static int satnogs_set_channel(RIG *rig, const channel_t *chan)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	if (!chan->ext_levels)
			return -RIG_EINVAL;

	if (chan->channel_num < 0 || chan->channel_num >= NB_CHAN)
			return -RIG_EINVAL;

	/* TODO:
	 * - check ext_levels is the right length
	 */
	switch (chan->vfo) {
		case RIG_VFO_MEM:
			copy_chan(&priv->mem[chan->channel_num], chan);
		break;
		case RIG_VFO_A:
			copy_chan(&priv->vfo_a, chan);
		break;
		case RIG_VFO_B:
			copy_chan(&priv->vfo_b, chan);
		break;
		case RIG_VFO_CURR:
			copy_chan(priv->curr, chan);
		break;
		default:
		return -RIG_EINVAL;
	}

	return RIG_OK;
}


static int satnogs_get_channel(RIG *rig, channel_t *chan)
{
	struct satnogs_priv_data *priv = (struct satnogs_priv_data *)rig->state.priv;

	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	if (chan->channel_num < 0 || chan->channel_num >= NB_CHAN)
			return -RIG_EINVAL;

	if (!chan->ext_levels) {
		 chan->ext_levels = alloc_init_ext(satnogs_ext_levels);
		 if (!chan->ext_levels)
				 return -RIG_ENOMEM;
	}

	/* TODO:
	 * - check ext_levels is the right length
	 */
	switch (chan->vfo) {
		case RIG_VFO_MEM:
			copy_chan(chan, &priv->mem[chan->channel_num]);
		break;
		case RIG_VFO_A:
			copy_chan(chan, &priv->vfo_a);
		break;
		case RIG_VFO_B:
			copy_chan(chan, &priv->vfo_b);
		break;
		case RIG_VFO_CURR:
			copy_chan(chan, priv->curr);
		break;
		default:
		return -RIG_EINVAL;
	}

	return RIG_OK;
}


static int satnogs_set_trn(RIG *rig, int trn)
{
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}


static int satnogs_get_trn(RIG *rig, int *trn)
{
	*trn = RIG_TRN_OFF;
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return RIG_OK;
}

static const char *satnogs_get_info(RIG *rig)
{
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);

	return "Nothing much (satnogs)";
}


static int satnogs_send_dtmf(RIG *rig, vfo_t vfo, const char *digits)
{
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, digits);

	return RIG_OK;
}

static int satnogs_recv_dtmf(RIG *rig, vfo_t vfo, char *digits, int *length)
{
	rig_debug(RIG_DEBUG_VERBOSE, "%s called\n", __FUNCTION__);
	strcpy(digits, "0123456789ABCDEF");
	*length = 16;

	return RIG_OK;
}

static int satnogs_send_morse(RIG *rig, vfo_t vfo, const char *msg)
{
	rig_debug(RIG_DEBUG_VERBOSE,"%s called: %s\n", __FUNCTION__, msg);

	return RIG_OK;
}

static int satnogs_power2mW(RIG * rig, unsigned int *mwpower, float power,
													freq_t freq, rmode_t mode)
{
		rig_debug(RIG_DEBUG_TRACE, "%s: passed power = %f\n", __func__, power);
		rig_debug(RIG_DEBUG_TRACE, "%s: passed freq = %"PRIfreq" Hz\n", __func__, freq);
		rig_debug(RIG_DEBUG_TRACE, "%s: passed mode = %i\n", __func__, mode);

		/* Pretend this is a 100W radio */
		*mwpower = (power * 100000);

		return RIG_OK;
}


static int satnogs_mW2power(RIG * rig, float *power, unsigned int mwpower,
													freq_t freq, rmode_t mode)
{
		rig_debug(RIG_DEBUG_TRACE, "%s: passed mwpower = %i\n", __func__, mwpower);
		rig_debug(RIG_DEBUG_TRACE, "%s: passed freq = %"PRIfreq" Hz\n", __func__, freq);
		rig_debug(RIG_DEBUG_TRACE, "%s: passed mode = %i\n", __func__, mode);

		/* Pretend this is a 100W radio */
		if (mwpower > 100000)
				return -RIG_EINVAL;

		*power = ((float)mwpower / 100000);

		return RIG_OK;
}


/*
 * Dummy rig capabilities.
 */

/*
 * The following macros set bitmasks for the various funcs, levels, parms,
 * etc.	This satnogs backend claims support for almost all of them.
 */
#define SATNOGS_FUNC	((setting_t)-1UL) /* All possible functions */
#define SATNOGS_LEVEL (((setting_t)-1UL)&~(1UL<<27)) /* All levels except SQLSTAT */
#define SATNOGS_PARM	((setting_t)-1UL) /* All possible parms */

#define SATNOGS_VFO_OP	0x7ffffffUL /* All possible VFO OPs */
#define SATNOGS_SCAN		0x7ffffffUL /* All possible scan OPs */

#define SATNOGS_VFOS (RIG_VFO_A|RIG_VFO_B|RIG_VFO_MEM)

#define SATNOGS_MODES (RIG_MODE_AM | RIG_MODE_CW | RIG_MODE_RTTY | \
										 RIG_MODE_SSB | RIG_MODE_FM | RIG_MODE_WFM | \
										 RIG_MODE_CWR | RIG_MODE_RTTYR)

#define SATNOGS_MEM_CAP {		\
	.bank_num = 1,	\
	.vfo = 1,	\
	.ant = 1,	\
	.freq = 1,	\
	.mode = 1,	\
	.width = 1,	\
	.tx_freq = 1,	\
	.tx_mode = 1,	\
	.tx_width = 1,	\
	.split = 1,	\
	.rptr_shift = 1,	\
	.rptr_offs = 1,	\
	.tuning_step = 1,	\
	.rit = 1,	\
	.xit = 1,	\
	.funcs = SATNOGS_FUNC,	\
	.levels = RIG_LEVEL_SET(SATNOGS_LEVEL),	\
	.ctcss_tone = 1,	\
	.ctcss_sql = 1,	\
	.dcs_code = 1,	\
	.dcs_sql = 1,	\
	.scan_group = 1,	\
	.flags = 1,	\
	.channel_desc = 1,	\
	.ext_levels = 1,	\
	}

const struct rig_caps satnogs_caps = {
	.rig_model =			RIG_MODEL_SATNOGS,
	.model_name =		 "Satnogs",
	.mfg_name =			 "Satnogs",
	.version =				"0.5",
	.copyright =			"LGPL",
	.status =				 RIG_STATUS_BETA,
	.rig_type =			 RIG_TYPE_OTHER,
	.targetable_vfo = 	 0,
	.ptt_type =			 RIG_PTT_RIG,
	.dcd_type =			 RIG_DCD_RIG,
	.port_type =			RIG_PORT_NONE,
	.has_get_func =	 SATNOGS_FUNC,
	.has_set_func =	 SATNOGS_FUNC,
	.has_get_level =	SATNOGS_LEVEL,
	.has_set_level =	RIG_LEVEL_SET(SATNOGS_LEVEL),
	.has_get_parm = 	 SATNOGS_PARM,
	.has_set_parm = 	 RIG_PARM_SET(SATNOGS_PARM),
	.level_gran =		 { [LVL_CWPITCH] = { .step = { .i = 10 } } },
	.ctcss_list = 	 common_ctcss_list,
	.dcs_list =	 	 full_dcs_list,
	.chan_list = 	 {
			{	 0,	18, RIG_MTYPE_MEM, SATNOGS_MEM_CAP },
			{	19,	19, RIG_MTYPE_CALL },
			{	20,	NB_CHAN-1, RIG_MTYPE_EDGE },
			RIG_CHAN_END,
		 },
	.scan_ops = 	 SATNOGS_SCAN,
	.vfo_ops = 	 SATNOGS_VFO_OP,
	.transceive =		 RIG_TRN_OFF,
	.attenuator =		 { 10, 20, 30, RIG_DBLST_END, },
	.preamp = 		 { 10, RIG_DBLST_END, },
	.rx_range_list1 =	{ {.start=kHz(150),.end=MHz(1500),.modes=SATNOGS_MODES,
												.low_power=-1,.high_power=-1,SATNOGS_VFOS, RIG_ANT_1|RIG_ANT_2},
												RIG_FRNG_END, },
	.tx_range_list1 =	{ RIG_FRNG_END, },
	.rx_range_list2 =	{ {.start=kHz(150),.end=MHz(1500),.modes=SATNOGS_MODES,
												.low_power=-1,.high_power=-1,SATNOGS_VFOS, RIG_ANT_1|RIG_ANT_2},
												RIG_FRNG_END, },
	.tx_range_list2 =	{ RIG_FRNG_END, },
	.tuning_steps =	{ {SATNOGS_MODES,1}, {SATNOGS_MODES,RIG_TS_ANY}, RIG_TS_END, },
	.filters =	{
	{RIG_MODE_SSB|RIG_MODE_CW|RIG_MODE_RTTY, kHz(2.4)},
	{RIG_MODE_CW, Hz(500)},
	{RIG_MODE_AM, kHz(8)},
	{RIG_MODE_AM, kHz(2.4)},
	{RIG_MODE_FM, kHz(15)},
	{RIG_MODE_FM, kHz(8)},
	{RIG_MODE_WFM, kHz(230)},
	RIG_FLT_END,
	},
	.max_rit = 9990,
	.max_xit = 9990,
	.max_ifshift = 10000,
	.priv =	NULL,	/* priv */

	.extlevels =		satnogs_ext_levels,
	.extparms =		 satnogs_ext_parms,
	.cfgparams =		satnogs_cfg_params,

	.rig_init =		 satnogs_init,
	.rig_cleanup =	satnogs_cleanup,
	.rig_open =		 satnogs_open,
	.rig_close =		satnogs_close,

	.set_conf =		 satnogs_set_conf,
	.get_conf =		 satnogs_get_conf,

	.set_freq =		 satnogs_set_freq,
	.get_freq =		 satnogs_get_freq,
	.set_mode =		 satnogs_set_mode,
	.get_mode =		 satnogs_get_mode,
	.set_vfo =			satnogs_set_vfo,
	.get_vfo =			satnogs_get_vfo,

	.set_powerstat =	satnogs_set_powerstat,
	.get_powerstat =	satnogs_get_powerstat,
	.set_level =		 satnogs_set_level,
	.get_level =		 satnogs_get_level,
	.set_func =			satnogs_set_func,
	.get_func =			satnogs_get_func,
	.set_parm =			satnogs_set_parm,
	.get_parm =			satnogs_get_parm,
	.set_ext_level = satnogs_set_ext_level,
	.get_ext_level = satnogs_get_ext_level,
	.set_ext_parm =	satnogs_set_ext_parm,
	.get_ext_parm =	satnogs_get_ext_parm,

	.get_info =			satnogs_get_info,


	.set_ptt = 	satnogs_set_ptt,
	.get_ptt = 	satnogs_get_ptt,
	.get_dcd = 	satnogs_get_dcd,
	.set_rptr_shift = 	satnogs_set_rptr_shift,
	.get_rptr_shift = 	satnogs_get_rptr_shift,
	.set_rptr_offs = 	satnogs_set_rptr_offs,
	.get_rptr_offs = 	satnogs_get_rptr_offs,
	.set_ctcss_tone = 	satnogs_set_ctcss_tone,
	.get_ctcss_tone = 	satnogs_get_ctcss_tone,
	.set_dcs_code = 	satnogs_set_dcs_code,
	.get_dcs_code = 	satnogs_get_dcs_code,
	.set_ctcss_sql = 	satnogs_set_ctcss_sql,
	.get_ctcss_sql = 	satnogs_get_ctcss_sql,
	.set_dcs_sql = 	satnogs_set_dcs_sql,
	.get_dcs_sql = 	satnogs_get_dcs_sql,
	.set_split_freq = 	satnogs_set_split_freq,
	.get_split_freq = 	satnogs_get_split_freq,
	.set_split_mode = 	satnogs_set_split_mode,
	.get_split_mode = 	satnogs_get_split_mode,
	.set_split_vfo = 	satnogs_set_split_vfo,
	.get_split_vfo = 	satnogs_get_split_vfo,
	.set_rit = 	satnogs_set_rit,
	.get_rit = 	satnogs_get_rit,
	.set_xit = 	satnogs_set_xit,
	.get_xit = 	satnogs_get_xit,
	.set_ts = 	satnogs_set_ts,
	.get_ts = 	satnogs_get_ts,
	.set_ant = 	satnogs_set_ant,
	.get_ant = 	satnogs_get_ant,
	.set_bank = 	satnogs_set_bank,
	.set_mem = 	satnogs_set_mem,
	.get_mem = 	satnogs_get_mem,
	.vfo_op = 	satnogs_vfo_op,
	.scan = 		satnogs_scan,
	.send_dtmf =	satnogs_send_dtmf,
	.recv_dtmf =	satnogs_recv_dtmf,
	.send_morse =	satnogs_send_morse,
	.set_channel = 	satnogs_set_channel,
	.get_channel = 	satnogs_get_channel,
	.set_trn = 	satnogs_set_trn,
	.get_trn = 	satnogs_get_trn,
	.power2mW =	satnogs_power2mW,
	.mW2power =	satnogs_mW2power,
};

DECLARE_INITRIG_BACKEND(satnogs)
{
	rig_debug(RIG_DEBUG_VERBOSE, "satnogs: _init called\n");

	rig_register(&satnogs_caps);

	return RIG_OK;
}
