/* packet-etw.c
 * Routines for ETW Dissection
 *
 * Copyright 2020, Odysseus Yang
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Dissector based on ETW Trace
* https://docs.microsoft.com/en-us/windows/win32/etw/event-tracing-portal
*/

#include "config.h"

#include <epan/packet.h>
#include <wiretap/wtap.h>

void proto_register_etw(void);
void proto_reg_handoff_etw(void);

static int proto_etw = -1;
static int hf_etw_size = -1;
static int hf_etw_header_type = -1;
static int hf_etw_flags = -1;
static int hf_etw_event_property = -1;
static int hf_etw_thread_id = -1;
static int hf_etw_process_id = -1;
static int hf_etw_time_stamp = -1;
static int hf_etw_provider_id = -1;
static int hf_etw_buffer_context_processor_number = -1;
static int hf_etw_buffer_context_alignment = -1;
static int hf_etw_buffer_context_logger_id = -1;
static int hf_etw_message_length = -1;
static int hf_etw_provider_name_length = -1;
static int hf_etw_provider_name = -1;
static int hf_etw_message = -1;
static int hf_etw_user_data_length = -1;
static int hf_etw_descriptor_id = -1;
static int hf_etw_descriptor_version = -1;
static int hf_etw_descriptor_channel = -1;
static int hf_etw_descriptor_level = -1;
static int hf_etw_descriptor_opcode = -1;
static int hf_etw_descriptor_task = -1;
static int hf_etw_descriptor_keywords = -1;
static int hf_etw_processor_time = -1;
static int hf_etw_activity_id = -1;

static gint ett_etw_header = -1;
static gint ett_etw_descriptor = -1;
static gint ett_etw_buffer_context = -1;

static dissector_handle_t mbim_dissector;

static e_guid_t mbim_net_providerid = { 0xA42FE227, 0xA7BF, 0x4483, {0xA5, 0x02, 0x6B, 0xCD, 0xA4, 0x28, 0xCD, 0x96} };

#define ROUND_UP_COUNT(Count,Pow2) \
        ( ((Count)+(Pow2)-1) & (~(((int)(Pow2))-1)) )
#define ETW_HEADER_SIZE 0x60

static int etw_counter = 0;

static int
dissect_etw(tvbuff_t* tvb, packet_info* pinfo, proto_tree* tree _U_, void* data _U_)
{
    proto_tree* etw_header, * etw_descriptor, * etw_buffer_context;
    tvbuff_t* mbim_tvb;
    guint32 message_offset, message_length, provider_name_offset, provider_name_length, user_data_offset, user_data_length;
    e_guid_t provider_id;
    gint offset = 0;

    etw_header = proto_tree_add_subtree(tree, tvb, 0, ETW_HEADER_SIZE, ett_etw_header, NULL, "ETW Header");
    proto_tree_add_item(etw_header, hf_etw_size, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_header, hf_etw_header_type, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_header, hf_etw_flags, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_header, hf_etw_event_property, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_header, hf_etw_thread_id, tvb, offset, 4, ENC_LITTLE_ENDIAN);
    offset += 4;
    proto_tree_add_item(etw_header, hf_etw_process_id, tvb, offset, 4, ENC_LITTLE_ENDIAN);
    offset += 4;
    proto_tree_add_item(etw_header, hf_etw_time_stamp, tvb, offset, 8, ENC_LITTLE_ENDIAN);
    offset += 8;
    tvb_get_letohguid(tvb, offset, &provider_id);
    proto_tree_add_item(etw_header, hf_etw_provider_id, tvb, offset, 16, ENC_LITTLE_ENDIAN);
    offset += 16;

    etw_descriptor = proto_tree_add_subtree(etw_header, tvb, 40, 16, ett_etw_descriptor, NULL, "Descriptor");
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_id, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_version, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_channel, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_level, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_opcode, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_task, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item(etw_descriptor, hf_etw_descriptor_keywords, tvb, offset, 8, ENC_LITTLE_ENDIAN);
    offset += 8;

    proto_tree_add_item(etw_header, hf_etw_processor_time, tvb, offset, 8, ENC_LITTLE_ENDIAN);
    offset += 8;
    proto_tree_add_item(etw_header, hf_etw_activity_id, tvb, offset, 16, ENC_LITTLE_ENDIAN);
    offset += 16;

    etw_buffer_context = proto_tree_add_subtree(etw_header, tvb, 80, 4, ett_etw_descriptor, NULL, "Buffer Context");
    proto_tree_add_item(etw_buffer_context, hf_etw_buffer_context_processor_number, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_buffer_context, hf_etw_buffer_context_alignment, tvb, offset, 1, ENC_LITTLE_ENDIAN);
    offset += 1;
    proto_tree_add_item(etw_buffer_context, hf_etw_buffer_context_logger_id, tvb, offset, 2, ENC_LITTLE_ENDIAN);
    offset += 2;
    proto_tree_add_item_ret_uint(etw_header, hf_etw_user_data_length, tvb, offset, 4, ENC_LITTLE_ENDIAN, &user_data_length);
    offset += 4;
    proto_tree_add_item_ret_uint(etw_header, hf_etw_message_length, tvb, offset, 4, ENC_LITTLE_ENDIAN, &message_length);
    offset += 4;
    proto_tree_add_item_ret_uint(etw_header, hf_etw_provider_name_length, tvb, offset, 4, ENC_LITTLE_ENDIAN, &provider_name_length);
    offset += 4;
    user_data_offset = offset;
    message_offset = user_data_offset + ROUND_UP_COUNT(user_data_length, sizeof(gint32));
    if (message_length) {
        proto_tree_add_item(etw_header, hf_etw_message, tvb, message_offset, message_length, ENC_LITTLE_ENDIAN | ENC_UTF_16);
    }
    provider_name_offset = message_offset + ROUND_UP_COUNT(message_length, sizeof(gint32));
    if (provider_name_length) {
        proto_tree_add_item(etw_header, hf_etw_provider_name, tvb, provider_name_offset, provider_name_length, ENC_LITTLE_ENDIAN | ENC_UTF_16);
    }

    col_set_str(pinfo->cinfo, COL_DEF_SRC, "windows");
    col_set_str(pinfo->cinfo, COL_DEF_DST, "windows");
    if (memcmp(&mbim_net_providerid, &provider_id, sizeof(e_guid_t)) == 0) {
        guint32 pack_flags;

        if (WTAP_OPTTYPE_SUCCESS == wtap_block_get_uint32_option_value(pinfo->rec->block, OPT_PKT_FLAGS, &pack_flags)) {
            switch(PACK_FLAGS_DIRECTION(pack_flags)) {
                case PACK_FLAGS_DIRECTION_INBOUND:
                    col_set_str(pinfo->cinfo, COL_DEF_SRC, "device");
                    col_set_str(pinfo->cinfo, COL_DEF_DST, "host");
                    break;
                case PACK_FLAGS_DIRECTION_OUTBOUND:
                    col_set_str(pinfo->cinfo, COL_DEF_SRC, "host");
                    col_set_str(pinfo->cinfo, COL_DEF_DST, "device");
                    break;
            }
        }
        mbim_tvb = tvb_new_subset_remaining(tvb, user_data_offset);
        call_dissector_only(mbim_dissector, mbim_tvb, pinfo, tree, data);
    }
    else if (message_length){
        char* message = (char*)tvb_get_string_enc(pinfo->pool, tvb, message_offset, message_length, ENC_LITTLE_ENDIAN | ENC_UTF_16);
        col_set_str(pinfo->cinfo, COL_INFO, message);
        if (provider_name_offset) {
            char* provider_name = (char*)tvb_get_string_enc(pinfo->pool, tvb, provider_name_offset, provider_name_length, ENC_LITTLE_ENDIAN | ENC_UTF_16);
            col_set_str(pinfo->cinfo, COL_PROTOCOL, provider_name);
        }
    } else {
        col_set_str(pinfo->cinfo, COL_INFO, guids_resolve_guid_to_str(&provider_id, pinfo->pool));
    }

    etw_counter += 1;
    return tvb_captured_length(tvb);
}

void
proto_register_etw(void)
{
    static hf_register_info hf[] = {
        { &hf_etw_size,
            { "Size", "etw.size",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_header_type,
            { "Header Type", "etw.header_type",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_flags,
            { "Flags", "etw.flags",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_event_property,
            { "Event Property", "etw.event_property",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_thread_id,
            { "Thread ID", "etw.thread_id",
               FT_UINT32, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_process_id,
            { "Process ID", "etw.process_id",
               FT_UINT32, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_time_stamp,
            { "Time Stamp", "etw.time_stamp",
               FT_UINT64, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_provider_id,
            { "Provider ID", "etw.provider_id",
               FT_GUID, BASE_NONE, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_buffer_context_processor_number,
            { "Processor Number", "etw.buffer_context.processor_number",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_buffer_context_alignment,
            { "Alignment", "etw.buffer_context.alignment",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_buffer_context_logger_id,
            { "ID", "etw.buffer_context.logger_id",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_message_length,
            { "Message Length", "etw.message_length",
               FT_UINT32, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_provider_name_length,
            { "Provider Name Length", "etw.provider_name_length",
               FT_UINT32, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_provider_name,
            { "Provider Name", "etw.provider_name",
               FT_STRINGZ, BASE_NONE, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_message,
            { "Event Message", "etw.message",
               FT_STRINGZ, BASE_NONE, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_user_data_length,
            { "User Data Length", "etw.user_data_length",
               FT_UINT32, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_id,
            { "ID", "etw.descriptor.id",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_version,
            { "Version", "etw.descriptor.version",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_channel,
            { "Channel", "etw.descriptor.channel",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_level,
            { "Level", "etw.descriptor.level",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_opcode,
            { "Opcode", "etw.descriptor.opcode",
               FT_UINT8, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_task,
            { "Task", "etw.descriptor.task",
               FT_UINT16, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_descriptor_keywords,
            { "Keywords", "etw.descriptor.keywords",
               FT_UINT64, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_processor_time,
            { "Processor Time", "etw.processor_time",
               FT_UINT64, BASE_DEC, NULL, 0,
              NULL, HFILL }
        },
        { &hf_etw_activity_id,
            { "Activity ID", "etw.activity_id",
               FT_GUID, BASE_NONE, NULL, 0,
              NULL, HFILL }
        }
    };

    static gint *ett[] = {
        &ett_etw_header,
        &ett_etw_descriptor,
        &ett_etw_buffer_context
    };

    proto_etw = proto_register_protocol("Event Tracing for Windows", "ETW", "etw");
    proto_register_field_array(proto_etw, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_etw(void)
{
    static dissector_handle_t etw_handle;

    etw_handle = create_dissector_handle(dissect_etw, proto_etw);
    dissector_add_uint("wtap_encap", WTAP_ENCAP_ETW, etw_handle);

    mbim_dissector = find_dissector("mbim.control");
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
