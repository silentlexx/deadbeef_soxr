/*
    DeaDBeeF - The Ultimate Music Player
    Copyright (C) 2009-2013 Alexey Yakovenko <waker@users.sourceforge.net>
    Copyright (C) 2016 Alexey Makhno <silentlexx@gmail.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <soxr.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <deadbeef/deadbeef.h>

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_functions_t *deadbeef;


enum {
    PARAM_SAMPLERATE = 0,
    PARAM_QUALITY = 1,
    PARAM_STEEPFILTER = 2,
    PARAM_PHASE = 3,
    PARAM_ALLOW_ALIASING = 4,
    PARAM_SAMPLERATE2 = 5,
    PARAM_AUTOSAMPLERATE = 6,
    PARAM_COUNT
};

#define MIN_RATE 8000
#define MAX_RATE 192000


static DB_dsp_t plugin;

static soxr_t soxr;
static soxr_error_t error;


typedef struct {
    ddb_dsp_context_t ctx;
    int channels;
    int quality;
    int phase;
    int current_rate;
    int samplerate;
    int samplerate2;
    int steepfilter;   
    int allow_aliasing;
    int autosamplerate;
    unsigned need_reset : 1;

} ddb_soxr_opt_t;


int 
soxr_get_q (int val){
    switch (val) {
    case 0:
        return SOXR_QQ;
     break;
    case 1:
        return SOXR_LQ;
     break;
    case 2:
        return SOXR_MQ;
     break;
     case 3:
        return SOXR_HQ;
     break;
     case 4:
        return SOXR_VHQ;
     break;
     case 5:
        return SOXR_32_BITQ;
     break;
    default:
        return SOXR_VHQ;
    }
}

int 
soxr_get_p (int val){
    switch (val) {
    case 0:
        return SOXR_LINEAR_PHASE;
     break;
    case 1:
        return SOXR_INTERMEDIATE_PHASE;
     break;
    case 2:
        return SOXR_MINIMUM_PHASE;
     break;
    default:
        return SOXR_LINEAR_PHASE;
    }
}

int 
soxr_get_f (int val){
    if(val){
        return SOXR_FLOAT32_I;
    } else {
        return SOXR_INT16_I;
    }
 }

ddb_dsp_context_t*
ddb_soxr_open (void) {
    ddb_soxr_opt_t *opt = malloc (sizeof (ddb_soxr_opt_t));
    DDB_INIT_DSP_CONTEXT (opt,ddb_soxr_opt_t,&plugin);

    opt->samplerate = 48000;
    opt->samplerate2 = 44100;
    opt->quality = 4;
    opt->phase = 0;
    opt->steepfilter = 0;
    opt->allow_aliasing = 0;
    opt->autosamplerate = 0;
    opt->channels = -1;
    return (ddb_dsp_context_t *)opt;
}

void
ddb_soxr_close (ddb_dsp_context_t *_opt) {
    ddb_soxr_opt_t *opt = (ddb_soxr_opt_t*)_opt;
    soxr_delete (soxr);
    soxr = 0;
    soxr_clear(soxr);
    free (soxr);
    free (opt);
}

void
ddb_soxr_reset (ddb_dsp_context_t *_opt) {
    ddb_soxr_opt_t *opt = (ddb_soxr_opt_t*)_opt;
    opt->need_reset = 1;
}




int
ddb_soxr_can_bypass (ddb_dsp_context_t *_opt, ddb_waveformat_t *fmt) {
    ddb_soxr_opt_t *opt = (ddb_soxr_opt_t*)_opt;

    float samplerate = opt->current_rate;
    if (opt->autosamplerate) {
        DB_output_t *output = deadbeef->get_output ();
        samplerate = output->fmt.samplerate;
    }

    if (fmt->samplerate == samplerate) {
        return 1;
    }
    return 0;
}


int
ddb_soxr_process (ddb_dsp_context_t *_opt, float *samples, int nframes, int maxframes, ddb_waveformat_t *fmt, float *r) {
    ddb_soxr_opt_t *opt = (ddb_soxr_opt_t*)_opt;

    if (opt->autosamplerate) {
        DB_output_t *output = deadbeef->get_output ();
        if (output->fmt.samplerate <= 0) {
            return -1;
        }
       // trace ("soxr: autosamplerate=%d\n", output->fmt.samplerate);
        opt->current_rate = output->fmt.samplerate;
    } else {
        if(fmt->samplerate == 11025  || fmt->samplerate == 22050 || 
           fmt->samplerate == 44100  || fmt->samplerate == 88200 || 
           fmt->samplerate == 176400 || fmt->samplerate == 352800 
          ){
               opt->current_rate = opt->samplerate2;
           }  else {
              opt->current_rate = opt->samplerate;
          }
    }

    int new_rate = opt->current_rate;

   if (fmt->samplerate == new_rate || opt->quality==6) {
        return nframes;
    }

 double ratio = (double) new_rate / (double) fmt->samplerate;

 if ( opt->channels != fmt->channels || opt->need_reset || !soxr ) {


    soxr_delete (soxr);
    soxr = 0;   
    unsigned long quality_recipe;    
 
    soxr_io_spec_t io_spec;
    int io_format = soxr_get_f(fmt->is_float);
    io_spec = soxr_io_spec(io_format, io_format);
    /* Resample in one thread. Multithreading makes
     * performance worse with small chunks of audio. */
    soxr_runtime_spec_t runtime_spec;
    runtime_spec = soxr_runtime_spec(1);

     int qa = soxr_get_q(opt->quality);
     int pa = soxr_get_p(opt->phase);

    if(opt->steepfilter && opt->allow_aliasing){
        quality_recipe = qa | pa | SOXR_STEEP_FILTER | SOXR_ALLOW_ALIASING ;
    } else 
    if(opt->steepfilter && !opt->allow_aliasing){
        quality_recipe = qa | pa | SOXR_STEEP_FILTER ;
    } else 
    if(!opt->steepfilter && opt->allow_aliasing){
        quality_recipe = qa | pa | SOXR_ALLOW_ALIASING ;
    } else {
        quality_recipe = qa | pa ; 
    }

    soxr_quality_spec_t q = soxr_quality_spec(quality_recipe, 0);

    trace ("soxr: f=%d, q=%lu, ratio=%f, old_r=%d, new_r=%d\n", fmt->is_float, quality_recipe, ratio, fmt->samplerate, new_rate);
    soxr = soxr_create(fmt->samplerate, new_rate, fmt->channels, &error,  &io_spec, &q, &runtime_spec);
    if(!soxr){
        trace ("soxr create error!");
        return nframes;
    }
    opt->channels = fmt->channels;
    opt->need_reset = 0;
}

 //   fmt->samplerate = new_rate;

    size_t numoutframes = 0;
    size_t  outsize = (nframes*ratio);
    float outbuf[outsize*fmt->channels];
    memset (outbuf, 0, sizeof (outbuf));
    char *output = (char *)outbuf;
    float *input = samples;
    size_t  inputsize = nframes;
// size_t  samplesize = fmt->channels * sizeof (float);

    error = soxr_process (soxr, input , inputsize  , NULL, output, outsize, &numoutframes);

    memcpy (input, outbuf, numoutframes * fmt->channels * sizeof (float));

    fmt->samplerate = new_rate;
//    trace ("soxr: ratio=%f, in=%d, out=%d\n", ratio, nframes, numoutframes);
    return numoutframes;
}

int
ddb_soxr_num_params (void) {
    return PARAM_COUNT;
}

const char *
ddb_soxr_get_param_name (int p) {
    switch (p) {
    case PARAM_QUALITY:
        return "Quality";
    case PARAM_SAMPLERATE:
        return "Samplerate for 48000, 96000, 192000";
    case PARAM_SAMPLERATE2:
        return "Samplerate for 44100, 88200, 176400";
    case PARAM_STEEPFILTER:
         return "Steep Filter";
    case PARAM_PHASE:
        return "Phase";
    case PARAM_ALLOW_ALIASING:
        return "Allow Aliasing";
    case PARAM_AUTOSAMPLERATE:
        return "Auto samplerate";
    default:
        fprintf (stderr, "ddb_soxr_get_param_name: invalid param index (%d)\n", p);
    }
    return NULL;
}

void
ddb_soxr_set_param (ddb_dsp_context_t *ctx, int p, const char *val) {
    switch (p) {
    case PARAM_SAMPLERATE:
        ((ddb_soxr_opt_t*)ctx)->samplerate = atof (val);
        if (((ddb_soxr_opt_t*)ctx)->samplerate < MIN_RATE) {
            ((ddb_soxr_opt_t*)ctx)->samplerate = MIN_RATE;
        }
        if (((ddb_soxr_opt_t*)ctx)->samplerate > MAX_RATE) {
            ((ddb_soxr_opt_t*)ctx)->samplerate = MAX_RATE;
        }

        break;
    case PARAM_SAMPLERATE2:
        ((ddb_soxr_opt_t*)ctx)->samplerate2 = atof (val);
        if (((ddb_soxr_opt_t*)ctx)->samplerate2 < MIN_RATE) {
            ((ddb_soxr_opt_t*)ctx)->samplerate2 = MIN_RATE;
        }
        if (((ddb_soxr_opt_t*)ctx)->samplerate2 > MAX_RATE) {
            ((ddb_soxr_opt_t*)ctx)->samplerate2 = MAX_RATE;
        }

        break;
    case PARAM_QUALITY:
        ((ddb_soxr_opt_t*)ctx)->quality = atoi (val);
        ((ddb_soxr_opt_t*)ctx)->need_reset = 1;
        break;
    case PARAM_PHASE:
        ((ddb_soxr_opt_t*)ctx)->phase = atoi (val);
        ((ddb_soxr_opt_t*)ctx)->need_reset = 1;
    break;
    case PARAM_STEEPFILTER:
        ((ddb_soxr_opt_t*)ctx)->steepfilter = atoi (val);   
        ((ddb_soxr_opt_t*)ctx)->need_reset = 1;
        break;
    case PARAM_ALLOW_ALIASING:
        ((ddb_soxr_opt_t*)ctx)->allow_aliasing = atoi (val);   
        ((ddb_soxr_opt_t*)ctx)->need_reset = 1;
        break;
    case PARAM_AUTOSAMPLERATE:
        ((ddb_soxr_opt_t*)ctx)->autosamplerate = atoi (val);
        break;
    default:
        fprintf (stderr, "ddb_soxr_set_param: invalid param index (%d)\n", p);
    }
}

void
ddb_soxr_get_param (ddb_dsp_context_t *ctx, int p, char *val, int sz) {
    switch (p) {
    case PARAM_SAMPLERATE:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->samplerate);
        break;
    case PARAM_SAMPLERATE2:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->samplerate2);
        break;
    case PARAM_QUALITY:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->quality);
        break;
    case PARAM_PHASE:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->phase);
        break;
    case PARAM_STEEPFILTER:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->steepfilter);
        break;
    case PARAM_ALLOW_ALIASING:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->allow_aliasing);
        break;
    case PARAM_AUTOSAMPLERATE:
        snprintf (val, sz, "%d", ((ddb_soxr_opt_t*)ctx)->autosamplerate);
        break;
    default:
        fprintf (stderr, "ddb_soxr_get_param: invalid param index (%d)\n", p);
    }
}




static const char settings_dlg[] =
    "property \"Automatic Samplerate (overrides Target Samplerate)\" checkbox 6 0;\n"
    "property \"Target Samplerate for 48000, 96000, 192000\" spinbtn[8000,192000,1] 0 48000;\n"
    "property \"Target Samplerate for 44100, 88200, 176400\" spinbtn[8000,192000,1] 5 44100;\n"
    "property \"Quality / Algorithm\" select[7] 1 4 QQ LQ MQ HQ VHQ 32BIT DISABLE;\n"
    "property \"Phase\" select[3] 3 0 LINEAR INTERMEDIATE MINIMUM;\n"
    "property \"Steep Filter\" checkbox 2 0;\n"
    "property \"Allow Aliasing (Reserved for future use)\" checkbox 4 0;\n"
;

static DB_dsp_t plugin = {
    // need 1.1 api for pass_through
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 1,
    .open = ddb_soxr_open,
    .close = ddb_soxr_close,
    .process = ddb_soxr_process,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.type = DB_PLUGIN_DSP,
    .plugin.id = "SOXR",
    .plugin.name = "SoX Resampler",
    .plugin.descr = "High quality samplerate converter using SoX Resampler, http://sourceforge.net/projects/soxr/",
    .plugin.copyright = 
        "Copyright (C) 2016 Alexey Makhno <silentlexx@gmail.com>\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website = "http://deadbeef.sf.net",
    .num_params = ddb_soxr_num_params,
    .get_param_name = ddb_soxr_get_param_name,
    .set_param = ddb_soxr_set_param,
    .get_param = ddb_soxr_get_param,
    .reset = ddb_soxr_reset,
    .configdialog = settings_dlg,
    .can_bypass = ddb_soxr_can_bypass,
};

DB_plugin_t *
ddb_soxr_dsp_load (DB_functions_t *f) {
    deadbeef = f;
    return &plugin.plugin;
}
