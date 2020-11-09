/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsenb/hdr/stack/rrc/mac_controller.h"
#include "srslte/asn1/rrc_utils.h"
#include "srslte/interfaces/sched_interface.h"

namespace srsenb {

using namespace asn1::rrc;
using ue_cfg_t = sched_interface::ue_cfg_t;

/********* Helper Methods ********/

// TS 36.331 9.1.1.2 - CCCH configuration
sched_interface::ue_bearer_cfg_t get_bearer_default_ccch_config()
{
  sched_interface::ue_bearer_cfg_t bearer = {};
  bearer.direction                        = sched_interface::ue_bearer_cfg_t::BOTH;
  bearer.priority                         = 1;
  bearer.pbr                              = -1;
  bearer.bsd                              = -1;
  bearer.group                            = 0;
  return bearer;
}

// TS 36.331 9.2.1.1 - SRB1
sched_interface::ue_bearer_cfg_t get_bearer_default_srb1_config()
{
  return get_bearer_default_ccch_config();
}

// TS 36.331 9.2.1.2 - SRB2
sched_interface::ue_bearer_cfg_t get_bearer_default_srb2_config()
{
  sched_interface::ue_bearer_cfg_t bearer = get_bearer_default_srb1_config();
  bearer.priority                         = 3;
  return bearer;
}

void ue_cfg_apply_srb_updates(ue_cfg_t& ue_cfg, const srb_to_add_mod_list_l& srbs);

/**
 * Adds to sched_interface::ue_cfg_t the changes present in the asn1 RRCReconfiguration message that should
 * only take effect after the RRCReconfigurationComplete is received
 */
void ue_cfg_apply_reconf_complete_updates(ue_cfg_t&                            ue_cfg,
                                          const rrc_conn_recfg_r8_ies_s&       conn_recfg,
                                          const cell_ctxt_dedicated_list&      ue_cell_list,
                                          const srslte::rrc_ue_capabilities_t& uecaps);

/**
 * Adds to sched_interface::ue_cfg_t the changes present in the asn1 RRCReconfiguration message related to
 * PHY ConfigurationDedicated
 */
void ue_cfg_apply_phy_cfg_ded(ue_cfg_t& ue_cfg, const asn1::rrc::phys_cfg_ded_s& phy_cfg, const rrc_cfg_t& rrc_cfg);

/***************************
 *  MAC Controller class
 **************************/

rrc::ue::mac_controller::mac_controller(rrc::ue* rrc_ue_, const ue_cfg_t& sched_ue_cfg) :
  log_h("RRC"),
  rrc_ue(rrc_ue_),
  rrc_cfg(&rrc_ue_->parent->cfg),
  mac(rrc_ue_->parent->mac),
  current_sched_ue_cfg(sched_ue_cfg)
{
  if (current_sched_ue_cfg.supported_cc_list.empty() or not current_sched_ue_cfg.supported_cc_list[0].active) {
    log_h->warning("No PCell set. Picking enb_cc_idx=0 as PCell\n");
    current_sched_ue_cfg.supported_cc_list.resize(1);
    current_sched_ue_cfg.supported_cc_list[0].active     = true;
    current_sched_ue_cfg.supported_cc_list[0].enb_cc_idx = 0;
  }
}

int rrc::ue::mac_controller::handle_con_setup(const asn1::rrc::rrc_conn_setup_r8_ies_s& conn_setup)
{
  return apply_basic_conn_cfg(conn_setup.rr_cfg_ded);
}

int rrc::ue::mac_controller::handle_con_reest(const asn1::rrc::rrc_conn_reest_r8_ies_s& conn_reest)
{
  return apply_basic_conn_cfg(conn_reest.rr_cfg_ded);
}

//! Called when ConnectionSetup or Reestablishment is rejected (e.g. no MME connection)
void rrc::ue::mac_controller::handle_con_reject()
{
  if (not crnti_set) {
    crnti_set = true;
    // Need to schedule ConRes CE for UE to see the Reject message
    mac->ue_set_crnti(rrc_ue->rnti, rrc_ue->rnti, &current_sched_ue_cfg);
  }
}

/// Called in case of intra-eNB Handover to activate the new PCell for the reception of the RRC Reconf Complete message
int rrc::ue::mac_controller::handle_crnti_ce(uint32_t temp_crnti)
{
  // Change PCell and add SCell configurations to MAC/Scheduler
  current_sched_ue_cfg = next_sched_ue_cfg;

  // Disable SCells, until RRCReconfComplete is received, otherwise the SCell Act MAC CE is sent too early
  for (uint32_t i = 1; i < current_sched_ue_cfg.supported_cc_list.size(); ++i) {
    current_sched_ue_cfg.supported_cc_list[i].active = false;
  }

  // keep DRBs disabled until RRCReconfComplete is received
  set_drb_activation(false);

  // Re-activate SRBs UL (needed for ReconfComplete)
  for (uint32_t i = rb_id_t::RB_ID_SRB1; i <= rb_id_t::RB_ID_SRB2; ++i) {
    current_sched_ue_cfg.ue_bearers[i] = next_sched_ue_cfg.ue_bearers[i];
  }

  return mac->ue_set_crnti(temp_crnti, rrc_ue->rnti, &current_sched_ue_cfg);
}

int rrc::ue::mac_controller::apply_basic_conn_cfg(const asn1::rrc::rr_cfg_ded_s& rr_cfg)
{
  const auto* pcell = rrc_ue->cell_ded_list.get_ue_cc_idx(UE_PCELL_CC_IDX);

  // Set static config params
  current_sched_ue_cfg.maxharq_tx       = rrc_cfg->mac_cnfg.ul_sch_cfg.max_harq_tx.to_number();
  current_sched_ue_cfg.continuous_pusch = false;

  // Only PCell active at this point
  current_sched_ue_cfg.supported_cc_list.resize(1, {});
  current_sched_ue_cfg.supported_cc_list[0].active     = true;
  current_sched_ue_cfg.supported_cc_list[0].enb_cc_idx = pcell->cell_common->enb_cc_idx;

  // Only SRB0 and SRB1 active in the Scheduler at this point
  current_sched_ue_cfg.ue_bearers    = {};
  current_sched_ue_cfg.ue_bearers[0] = get_bearer_default_ccch_config();
  current_sched_ue_cfg.ue_bearers[1] = get_bearer_default_srb1_config();

  // Set default configuration
  current_sched_ue_cfg.supported_cc_list[0].dl_cfg.tm = SRSLTE_TM1;
  current_sched_ue_cfg.use_tbs_index_alt              = false;

  // Apply common PhyConfig updates (e.g. SR/CQI resources, antenna cfg)
  if (rr_cfg.phys_cfg_ded_present) {
    ue_cfg_apply_phy_cfg_ded(current_sched_ue_cfg, rr_cfg.phys_cfg_ded, *rrc_cfg);
  }

  // Other PUCCH params
  const asn1::rrc::sib_type2_s& sib2               = pcell->cell_common->sib2;
  current_sched_ue_cfg.pucch_cfg.delta_pucch_shift = sib2.rr_cfg_common.pucch_cfg_common.delta_pucch_shift.to_number();
  current_sched_ue_cfg.pucch_cfg.N_cs              = sib2.rr_cfg_common.pucch_cfg_common.ncs_an;
  current_sched_ue_cfg.pucch_cfg.n_rb_2            = sib2.rr_cfg_common.pucch_cfg_common.nrb_cqi;
  current_sched_ue_cfg.pucch_cfg.N_pucch_1         = sib2.rr_cfg_common.pucch_cfg_common.n1_pucch_an;

  // PUSCH UCI configuration
  current_sched_ue_cfg.uci_offset.I_offset_cqi = rrc_cfg->pusch_cfg.beta_offset_cqi_idx;
  current_sched_ue_cfg.uci_offset.I_offset_ack = rrc_cfg->pusch_cfg.beta_offset_ack_idx;
  current_sched_ue_cfg.uci_offset.I_offset_ri  = rrc_cfg->pusch_cfg.beta_offset_ri_idx;

  // Configure MAC
  // In case of RRC Connection Setup/Reest message (Msg4), we need to resolve the contention by sending a ConRes CE
  mac->phy_config_enabled(rrc_ue->rnti, false);
  crnti_set = true;
  return mac->ue_set_crnti(rrc_ue->rnti, rrc_ue->rnti, &current_sched_ue_cfg);
}

void rrc::ue::mac_controller::handle_con_setup_complete()
{
  // Acknowledge Dedicated Configuration
  mac->phy_config_enabled(rrc_ue->rnti, true);
}

void rrc::ue::mac_controller::handle_con_reest_complete()
{
  // Acknowledge Dedicated Configuration
  mac->phy_config_enabled(rrc_ue->rnti, true);
}

void rrc::ue::mac_controller::handle_con_reconf(const asn1::rrc::rrc_conn_recfg_r8_ies_s& conn_recfg,
                                                const srslte::rrc_ue_capabilities_t&      uecaps)
{
  if (conn_recfg.rr_cfg_ded_present and conn_recfg.rr_cfg_ded.phys_cfg_ded_present) {
    ue_cfg_apply_phy_cfg_ded(current_sched_ue_cfg, conn_recfg.rr_cfg_ded.phys_cfg_ded, *rrc_cfg);
  }

  // Store MAC updates that are applied once RRCReconfigurationComplete is received
  next_sched_ue_cfg = current_sched_ue_cfg;
  ue_cfg_apply_reconf_complete_updates(next_sched_ue_cfg, conn_recfg, rrc_ue->cell_ded_list, uecaps);

  // Temporarily freeze new allocations for DRBs (SRBs are needed to send RRC Reconf Message)
  set_drb_activation(false);

  // Apply changes to MAC scheduler
  update_mac(proc_stage_t::config_tx);
}

void rrc::ue::mac_controller::handle_con_reconf_complete()
{
  current_sched_ue_cfg = next_sched_ue_cfg;

  // Setup all bearers
  apply_current_bearers_cfg();

  // Apply SCell+Bearer changes to MAC
  update_mac(proc_stage_t::config_complete);
}

void rrc::ue::mac_controller::apply_current_bearers_cfg()
{
  // Configure DRBs
  const drb_to_add_mod_list_l& drbs = rrc_ue->bearer_list.get_established_drbs();
  for (const drb_to_add_mod_s& drb : drbs) {
    auto& bcfg     = current_sched_ue_cfg.ue_bearers[drb.lc_ch_id];
    bcfg           = {};
    bcfg.direction = sched_interface::ue_bearer_cfg_t::BOTH;
    bcfg.group     = 1;
    bcfg.priority  = 4;
    if (drb.lc_ch_cfg_present and drb.lc_ch_cfg.ul_specific_params_present) {
      bcfg.group    = drb.lc_ch_cfg.ul_specific_params.lc_ch_group;
      bcfg.pbr      = drb.lc_ch_cfg.ul_specific_params.prioritised_bit_rate.to_number();
      bcfg.priority = drb.lc_ch_cfg.ul_specific_params.prio;
      bcfg.bsd      = drb.lc_ch_cfg.ul_specific_params.bucket_size_dur;
    }
  }
}

void rrc::ue::mac_controller::handle_target_enb_ho_cmd(const asn1::rrc::rrc_conn_recfg_r8_ies_s& conn_recfg,
                                                       const srslte::rrc_ue_capabilities_t&      uecaps)
{
  if (conn_recfg.rr_cfg_ded_present and conn_recfg.rr_cfg_ded.phys_cfg_ded_present) {
    ue_cfg_apply_phy_cfg_ded(current_sched_ue_cfg, conn_recfg.rr_cfg_ded.phys_cfg_ded, *rrc_cfg);
  }

  next_sched_ue_cfg = current_sched_ue_cfg;
  ue_cfg_apply_reconf_complete_updates(next_sched_ue_cfg, conn_recfg, rrc_ue->cell_ded_list, uecaps);

  // Temporarily freeze new allocations for DRBs (SRBs are needed to send RRC Reconf Message)
  set_drb_activation(false);

  // Apply changes to MAC scheduler
  mac->ue_cfg(rrc_ue->rnti, &current_sched_ue_cfg);
  mac->phy_config_enabled(rrc_ue->rnti, false);
}

void rrc::ue::mac_controller::handle_intraenb_ho_cmd(const asn1::rrc::rrc_conn_recfg_r8_ies_s& conn_recfg,
                                                     const srslte::rrc_ue_capabilities_t&      uecaps)
{
  next_sched_ue_cfg = current_sched_ue_cfg;
  next_sched_ue_cfg.supported_cc_list.resize(1);
  next_sched_ue_cfg.supported_cc_list[0].active = true;
  next_sched_ue_cfg.supported_cc_list[0].enb_cc_idx =
      rrc_ue->parent->cell_common_list->get_pci(conn_recfg.mob_ctrl_info.target_pci)->enb_cc_idx;
  if (conn_recfg.rr_cfg_ded_present and conn_recfg.rr_cfg_ded.phys_cfg_ded_present) {
    ue_cfg_apply_phy_cfg_ded(next_sched_ue_cfg, conn_recfg.rr_cfg_ded.phys_cfg_ded, *rrc_cfg);
  }
  ue_cfg_apply_reconf_complete_updates(next_sched_ue_cfg, conn_recfg, rrc_ue->cell_ded_list, uecaps);

  // Freeze SCells
  // NOTE: this avoids that the UE receives an HOCmd retx from target cell and do an incorrect RLC-level concatenation
  set_scell_activation({0});

  // Freeze all DRBs. SRBs DL are needed for sending the HO Cmd
  set_drb_activation(false);

  // Stop any SRB UL (including SRs)
  for (uint32_t i = rb_id_t::RB_ID_SRB1; i <= rb_id_t::RB_ID_SRB2; ++i) {
    next_sched_ue_cfg.ue_bearers[i].direction = sched_interface::ue_bearer_cfg_t::DL;
  }

  update_mac(mac_controller::config_tx);
}

void rrc::ue::mac_controller::handle_ho_prep(const asn1::rrc::ho_prep_info_r8_ies_s& ho_prep)
{
  // TODO: Apply configuration in ho_prep as a base
  if (ho_prep.as_cfg.source_rr_cfg.srb_to_add_mod_list_present) {
    ue_cfg_apply_srb_updates(current_sched_ue_cfg, ho_prep.as_cfg.source_rr_cfg.srb_to_add_mod_list);
  }
}

void rrc::ue::mac_controller::set_scell_activation(const std::bitset<SRSLTE_MAX_CARRIERS>& scell_mask)
{
  for (uint32_t i = 1; i < current_sched_ue_cfg.supported_cc_list.size(); ++i) {
    current_sched_ue_cfg.supported_cc_list[i].active = scell_mask[i];
  }
}

void rrc::ue::mac_controller::set_drb_activation(bool active)
{
  for (const drb_to_add_mod_s& drb : rrc_ue->bearer_list.get_established_drbs()) {
    current_sched_ue_cfg.ue_bearers[drb.drb_id + rb_id_t::RB_ID_SRB2].direction =
        active ? sched_interface::ue_bearer_cfg_t::BOTH : sched_interface::ue_bearer_cfg_t::IDLE;
  }
}

void rrc::ue::mac_controller::update_mac(proc_stage_t stage)
{
  // Apply changes to MAC scheduler
  mac->ue_cfg(rrc_ue->rnti, &current_sched_ue_cfg);
  if (stage != proc_stage_t::other) {
    // Acknowledge Dedicated Configuration
    mac->phy_config_enabled(rrc_ue->rnti, stage == proc_stage_t::config_complete);
  }
}

void ue_cfg_apply_phy_cfg_ded(ue_cfg_t& ue_cfg, const asn1::rrc::phys_cfg_ded_s& phy_cfg, const rrc_cfg_t& rrc_cfg)
{
  // Apply SR config
  if (phy_cfg.sched_request_cfg_present) {
    ue_cfg.pucch_cfg.sr_configured = true;
    ue_cfg.pucch_cfg.I_sr          = phy_cfg.sched_request_cfg.setup().sr_cfg_idx;
    ue_cfg.pucch_cfg.n_pucch_sr    = phy_cfg.sched_request_cfg.setup().sr_pucch_res_idx;
  }

  // Apply CQI config
  auto& pcell_cfg = ue_cfg.supported_cc_list[0];
  if (phy_cfg.cqi_report_cfg_present) {
    if (phy_cfg.cqi_report_cfg.cqi_report_periodic_present) {
      auto& cqi_cfg                                   = phy_cfg.cqi_report_cfg.cqi_report_periodic.setup();
      ue_cfg.pucch_cfg.n_pucch                        = cqi_cfg.cqi_pucch_res_idx;
      pcell_cfg.dl_cfg.cqi_report.pmi_idx             = cqi_cfg.cqi_pmi_cfg_idx;
      pcell_cfg.dl_cfg.cqi_report.periodic_configured = true;
    } else if (phy_cfg.cqi_report_cfg.cqi_report_mode_aperiodic_present) {
      pcell_cfg.aperiodic_cqi_period                   = rrc_cfg.cqi_cfg.period;
      pcell_cfg.dl_cfg.cqi_report.aperiodic_configured = true;
    }
  }

  // Apply Antenna Configuration
  if (phy_cfg.ant_info_present) {
    if (phy_cfg.ant_info.type().value == phys_cfg_ded_s::ant_info_c_::types_opts::explicit_value) {
      ue_cfg.dl_ant_info = srslte::make_ant_info_ded(phy_cfg.ant_info.explicit_value());
    } else {
      srslte::logmap::get("RRC")->warning("No antenna configuration provided\n");
      pcell_cfg.dl_cfg.tm        = SRSLTE_TM1;
      ue_cfg.dl_ant_info.tx_mode = sched_interface::ant_info_ded_t::tx_mode_t::tm1;
    }
  }
}

void ue_cfg_apply_srb_updates(ue_cfg_t& ue_cfg, const srb_to_add_mod_list_l& srbs)
{
  for (const srb_to_add_mod_s& srb : srbs) {
    auto& bcfg = ue_cfg.ue_bearers[srb.srb_id];
    switch (srb.srb_id) {
      case 1:
        bcfg = get_bearer_default_srb1_config();
        break;
      case 2:
        bcfg = get_bearer_default_srb2_config();
        break;
      default:
        srslte::logmap::get("RRC")->warning("Invalid SRB ID %d\n", (int)srb.srb_id);
        bcfg = {};
    }
    bcfg.direction = srsenb::sched_interface::ue_bearer_cfg_t::BOTH;
    if (srb.lc_ch_cfg_present and
        srb.lc_ch_cfg.type().value == srb_to_add_mod_s::lc_ch_cfg_c_::types_opts::explicit_value and
        srb.lc_ch_cfg.explicit_value().ul_specific_params_present) {
      // NOTE: Use UL values for DL prioritization as well
      auto& ul_params = srb.lc_ch_cfg.explicit_value().ul_specific_params;
      bcfg.pbr        = ul_params.prioritised_bit_rate.to_number();
      bcfg.priority   = ul_params.prio;
      bcfg.bsd        = ul_params.bucket_size_dur.to_number();
      if (ul_params.lc_ch_group_present) {
        bcfg.group = ul_params.lc_ch_group;
      }
    }
  }
}

void ue_cfg_apply_reconf_complete_updates(ue_cfg_t&                            ue_cfg,
                                          const rrc_conn_recfg_r8_ies_s&       conn_recfg,
                                          const cell_ctxt_dedicated_list&      ue_cell_list,
                                          const srslte::rrc_ue_capabilities_t& uecaps)
{
  // Configure RadioResourceConfigDedicated
  if (conn_recfg.rr_cfg_ded_present) {
    if (conn_recfg.rr_cfg_ded.phys_cfg_ded_present) {
      auto& phy_cfg = conn_recfg.rr_cfg_ded.phys_cfg_ded;

      // Configure 256QAM
      if (phy_cfg.cqi_report_cfg_pcell_v1250.is_present() and
          phy_cfg.cqi_report_cfg_pcell_v1250->alt_cqi_table_r12_present) {
        ue_cfg.use_tbs_index_alt = true;
      }

      // PUSCH UCI configuration
      if (phy_cfg.pusch_cfg_ded_present) {
        ue_cfg.uci_offset.I_offset_cqi = phy_cfg.pusch_cfg_ded.beta_offset_cqi_idx;
        ue_cfg.uci_offset.I_offset_ack = phy_cfg.pusch_cfg_ded.beta_offset_ack_idx;
        ue_cfg.uci_offset.I_offset_ri  = phy_cfg.pusch_cfg_ded.beta_offset_ri_idx;
      }
    }

    // Apply SRB updates
    if (conn_recfg.rr_cfg_ded.srb_to_add_mod_list_present) {
      ue_cfg_apply_srb_updates(ue_cfg, conn_recfg.rr_cfg_ded.srb_to_add_mod_list);
    }
  }

  // Apply Scell configurations
  if (conn_recfg.non_crit_ext_present and conn_recfg.non_crit_ext.non_crit_ext_present and
      conn_recfg.non_crit_ext.non_crit_ext.non_crit_ext_present and
      conn_recfg.non_crit_ext.non_crit_ext.non_crit_ext.scell_to_add_mod_list_r10_present) {
    for (const auto& scell : conn_recfg.non_crit_ext.non_crit_ext.non_crit_ext.scell_to_add_mod_list_r10) {

      // Resize MAC SCell list if required
      if (scell.scell_idx_r10 >= ue_cfg.supported_cc_list.size()) {
        ue_cfg.supported_cc_list.resize(scell.scell_idx_r10 + 1);
      }
      auto& mac_scell      = ue_cfg.supported_cc_list[scell.scell_idx_r10];
      mac_scell.active     = true;
      mac_scell.enb_cc_idx = ue_cell_list.get_ue_cc_idx(scell.scell_idx_r10)->cell_common->enb_cc_idx;

      if (scell.rr_cfg_ded_scell_r10_present and scell.rr_cfg_ded_scell_r10.phys_cfg_ded_scell_r10_present and
          scell.rr_cfg_ded_scell_r10.phys_cfg_ded_scell_r10.ul_cfg_r10_present) {
        const auto& ul_cfg = scell.rr_cfg_ded_scell_r10.phys_cfg_ded_scell_r10.ul_cfg_r10;

        // Set CQI Config
        if (ul_cfg.cqi_report_cfg_scell_r10_present) {
          if (ul_cfg.cqi_report_cfg_scell_r10.cqi_report_periodic_scell_r10_present and
              ul_cfg.cqi_report_cfg_scell_r10.cqi_report_periodic_scell_r10.type().value == setup_opts::setup) {
            // periodic CQI
            auto& periodic = ul_cfg.cqi_report_cfg_scell_r10.cqi_report_periodic_scell_r10.setup();
            mac_scell.dl_cfg.cqi_report.periodic_configured = true;
            mac_scell.dl_cfg.cqi_report.pmi_idx             = periodic.cqi_pmi_cfg_idx;
          } else if (ul_cfg.cqi_report_cfg_scell_r10.cqi_report_mode_aperiodic_r10_present) {
            // aperiodic CQI
            mac_scell.dl_cfg.cqi_report.aperiodic_configured =
                ul_cfg.cqi_report_cfg_scell_r10.cqi_report_mode_aperiodic_r10_present;
            mac_scell.aperiodic_cqi_period = ul_cfg.cqi_report_cfg_scell_r10.cqi_report_mode_aperiodic_r10.to_number();
          } else {
            srslte::logmap::get("RRC")->warning("Invalid Scell index %d CQI configuration\n", scell.scell_idx_r10);
          }
        }
      }
    }
  }

  // Params non-dependent on Reconf message content
  if (uecaps.support_ul_64qam) {
    ue_cfg.support_ul_64qam = true;
  }
}

} // namespace srsenb
