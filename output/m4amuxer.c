/*****************************************************************************
 * mp4amuxer.c:
 *****************************************************************************
 * Copyright (C) 2010 L-SMASH project
 *
 * Authors: Takashi Hirata <silverfilain AT gmail DOT com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "isom.h"
#include <stdarg.h>

#define eprintf( ... ) fprintf( stderr, __VA_ARGS__ )

typedef struct
{
    mp4sys_importer_t* importer;
    mp4sys_audio_summary_t* summary;
    isom_root_t* root;
} structs_t;

static void cleanup_structs( structs_t* structs )
{
    if( !structs )
        return;
    if( structs->root )
        isom_destroy_root( structs->root );
    if( structs->summary )
        mp4sys_cleanup_audio_summary( structs->summary );
    if( structs->importer )
        mp4sys_importer_close( structs->importer );
}

static int m4amux_error( structs_t* structs, char* msg )
{
    cleanup_structs( structs );
    eprintf( msg );
    return -1;
}

#define M4AMUX_ERR( msg ) m4amux_error( &structs, msg )
#define M4AMUX_USAGE_ERR() M4AMUX_ERR( "Usage: m4amuxer [--sbr] [--3gp|--3g2] input output\n" )

int main( int argc, char* argv[] )
{

    structs_t structs = { 0 };
    int sbr = 0;
    int brand_3gx = 0;

    if( argc < 3 )
        return M4AMUX_USAGE_ERR();

    int i = 1;
    while( *argv[i] == '-' )
    {
        if( argc <= i + 2 )
            return M4AMUX_USAGE_ERR();
        if( !strcasecmp( argv[i], "--sbr" ) )
        {
            sbr = 1;
            eprintf( "Using backward-compatible SBR explicit signaling mode.\n" );
        }
        else if( !strcasecmp( argv[i], "--3gp" ) )
        {
            brand_3gx = 1;
            eprintf( "Using 3gp muxing mode.\n" );
        }
        else if( !strcasecmp( argv[i], "--3g2" ) )
        {
            brand_3gx = 2;
            eprintf( "Using 3g2 muxing mode.\n" );
        }
        else
            return M4AMUX_USAGE_ERR();
        i++;
    }

    /* Initialize importer framework */
    structs.importer = mp4sys_importer_open( argv[i++], "auto" );
    if( !structs.importer || !(structs.summary = mp4sys_duplicate_audio_summary( structs.importer, 1 )) )
        return M4AMUX_ERR( "Failed to open input file.\n" );

    /* check codec type. */
    enum isom_codec_code codec_code;
    switch( structs.summary->object_type_indication )
    {
    case MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3:
    case MP4SYS_OBJECT_TYPE_Audio_ISO_13818_3: /* Legacy Interface */
    case MP4SYS_OBJECT_TYPE_Audio_ISO_11172_3: /* Legacy Interface */
        codec_code = ISOM_CODEC_TYPE_MP4A_AUDIO; break;
    case MP4SYS_OBJECT_TYPE_PRIV_SAMR_AUDIO:
        codec_code = ISOM_CODEC_TYPE_SAMR_AUDIO; break;
    default:
        return M4AMUX_ERR( "Unknown object_type_indication.\n" );
    }
    /* user defined sbr mode. */
    if( sbr )
    {
        if( structs.summary->object_type_indication != MP4SYS_OBJECT_TYPE_Audio_ISO_14496_3 )
            return M4AMUX_ERR( "Input file is not MP4A.\n" );
        structs.summary->sbr_mode = MP4A_AAC_SBR_BACKWARD_COMPATIBLE;
        if( mp4sys_setup_AudioSpecificConfig( structs.summary ) )
            return M4AMUX_ERR( "Failed to set SBR mode.\n" );
    }
    /* user defined brand mode. */
    if( brand_3gx )
    {
        if( structs.summary->frequency > 48000
            || ( sbr && structs.summary->frequency > 24000 ) )
            return M4AMUX_ERR( "3gp/3g2 does not allow frequency > 48000.\n" );
        if( structs.summary->channels > 2 )
            return M4AMUX_ERR( "3gp/3g2 does not allow channels > 2.\n" );
    }

    /* Initialize L-SMASH muxer */
    structs.root = isom_create_root( argv[i++] );
    if( !structs.root )
        return M4AMUX_ERR( "Failed to create root.\n" );

    /* FIXME: I think this does not make sense at all. track number must be retrieved in some other way.  */
    uint32_t track = 1 + isom_add_mandatory_boxes( structs.root, ISOM_HDLR_TYPE_AUDIO );
    if( !track )
        return M4AMUX_ERR( "Failed to add mandatory boxes.\n" );

    if( isom_set_movie_timescale( structs.root, 600 ) )
        return M4AMUX_ERR( "Failed to set movie timescale.\n" );

    /* Initialize track */
    if( isom_set_media_timescale( structs.root, track, structs.summary->frequency ) )
        return M4AMUX_ERR( "Failed to set media timescale.\n" );

    char handler_name[24] = "L-SMASH Audio Handler 1";
    if( isom_set_handler_name( structs.root, track, handler_name ) )
        return M4AMUX_ERR( "Failed to set handler name.\n" );

    uint32_t sample_entry = isom_add_sample_entry( structs.root, track, ISOM_CODEC_TYPE_MP4A_AUDIO, structs.summary );
    if( !sample_entry )
        return M4AMUX_ERR( "Failed to add sample_entry.\n" );

    /* Preparation for writing */
    uint32_t brands[5] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_M4A, ISOM_BRAND_TYPE_3GP6, ISOM_BRAND_TYPE_3G2A };
    int major_index = 2 + brand_3gx;
    int num_of_brands = 3 + brand_3gx;
    uint32_t minor_version = 1;
    if( brand_3gx == 1 )
        minor_version = 0x00000000; /* means, 3gp(3gp6) 6.0.0 : "6" is not included in minor_version. */
    else
        minor_version = 0x00010000; /* means, 3g2(3g2a) 1.0.0 : a == 1 */
    if( isom_set_brands( structs.root, brands[major_index], minor_version, brands, num_of_brands ) || isom_write_ftyp( structs.root ) )
        return M4AMUX_ERR( "Failed to set brands.\n" );
    if( isom_add_mdat( structs.root ) )
        return M4AMUX_ERR( "Failed to write mdat.\n" );

    /* transfer */
    uint32_t numframe = 0;
    isom_sample_property_t dependency = { 0 }; /* nothing */
    while(1)
    {
        /* allocate sample buffer */
        isom_sample_t *sample = isom_create_sample( structs.summary->max_au_length );
        if( !sample )
            return M4AMUX_ERR( "Failed to alloc memory for buffer.\n" );
        /* read a audio frame */
        /* FIXME: mp4sys_importer_get_access_unit() returns 1 if there're any changes in stream's properties.
           If you want to support them, you have to retrieve summary again, and make some operation accordingly. */
        sample->length = structs.summary->max_au_length;
        if( mp4sys_importer_get_access_unit( structs.importer, 1, sample->data, &sample->length ) )
        {
            isom_remove_sample( sample );
            return M4AMUX_ERR( "Failed to get a frame from input file. Maybe corrupted.\n" );
        }
        if( sample->length == 0 )
        {
            isom_remove_sample( sample );
            break; /* end of stream */
        }

        sample->dts = numframe * structs.summary->samples_in_frame;
        sample->cts = sample->dts;
        sample->index = sample_entry;
        sample->prop = dependency; /* every sample is a random access point. */
        if( isom_write_sample( structs.root, track, sample, 0.5 ) )
            return M4AMUX_ERR( "Failed to write a frame.\n" );
        numframe++;
        eprintf( "frame = %d\r", numframe );
    }
    eprintf( "total frames = %d\n", numframe );

    /* close track */
    if( isom_set_last_sample_delta( structs.root, track, structs.summary->samples_in_frame ) )
        eprintf( "Failed to set last sample's delta.\n" );
    // if( isom_update_track_duration( structs.root, track ) ) /* if not use edts */
    if( isom_create_explicit_timeline_map( structs.root, track, 0, 0, ISOM_NORMAL_EDIT ) ) /* use edts */
        eprintf( "Failed to set timeline map.\n" );
    if( isom_update_bitrate_info( structs.root, track, sample_entry ) )
        eprintf( "Failed to update bitrate info.\n" );

    /* close movie */
    if( isom_finish_movie( structs.root ) )
        eprintf( "Failed to finish movie.\n" );
    if( isom_write_mdat_size( structs.root ) )
        eprintf( "Failed to write mdat size.\n" );

    cleanup_structs( &structs ); /* including isom_destroy_root() */
    return 0;
}
