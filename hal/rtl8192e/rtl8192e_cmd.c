/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8192E_CMD_C_

//#include <drv_types.h>
#include <rtl8192e_hal.h>
#include "hal_com_h2c.h"
#include <hal_com.h>
#define CONFIG_H2C_EF

#define RTL8192E_MAX_H2C_BOX_NUMS	4
#define RTL8192E_MAX_CMD_LEN	7
#define RTL8192E_MESSAGE_BOX_SIZE		4
#define RTL8192E_EX_MESSAGE_BOX_SIZE	4


static u8 _is_fw_read_cmd_down(_adapter* padapter, u8 msgbox_num)
{
	u8	read_down = _FALSE;
	int 	retry_cnts = 100;

	u8 valid;

	//DBG_8192C(" _is_fw_read_cmd_down ,reg_1cc(%x),msg_box(%d)...\n",rtw_read8(padapter,REG_HMETFR),msgbox_num);

	do{
		valid = rtw_read8(padapter,REG_HMETFR) & BIT(msgbox_num);
		if(0 == valid ){
			read_down = _TRUE;
		}
		else
			rtw_msleep_os(1);
	}while( (!read_down) && (retry_cnts--));

	return read_down;

}


/*****************************************
* H2C Msg format :
* 0x1DF - 0x1D0
*| 31 - 8	| 7-5 	 4 - 0	|
*| h2c_msg 	|Class_ID CMD_ID	|
*
* Extend 0x1FF - 0x1F0
*|31 - 0	  |
*|ext_msg|
******************************************/
s32 FillH2CCmd_8192E(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 h2c_box_num = 0;
	u32	msgbox_addr = 0;
	u32 msgbox_ex_addr = 0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 cmd_idx = 0,ext_cmd_len = 0;
	u32	h2c_cmd = 0;
	u32	h2c_cmd_ex = 0;
	s32 ret = _FAIL;

_func_enter_;

	padapter = GET_PRIMARY_ADAPTER(padapter);		
	pHalData = GET_HAL_DATA(padapter);


	if(padapter->bFWReady == _FALSE)
	{
		DBG_8192C("FillH2CCmd_8192E(): return H2C cmd because fw is not ready\n");
		return ret;
	}

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

	if (!pCmdBuffer) {
		goto exit;
	}
	if (CmdLen > RTL8192E_MAX_CMD_LEN) {
		goto exit;
	}
	if (padapter->bSurpriseRemoved == _TRUE)
		goto exit;

	//pay attention to if  race condition happened in  H2C cmd setting.
	do{
		h2c_box_num = pHalData->LastHMEBoxNum;

		if(!_is_fw_read_cmd_down(padapter, h2c_box_num)){
			DBG_8192C(" fw read cmd failed...\n");
			goto exit;
		}

		*(u8*)(&h2c_cmd) = ElementID;

		if(CmdLen<=3)
		{
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer, CmdLen );
		}
		else{			
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer,3);
			ext_cmd_len = CmdLen-3;	
			_rtw_memcpy((u8*)(&h2c_cmd_ex), pCmdBuffer+3,ext_cmd_len );

			//Write Ext command
			msgbox_ex_addr = REG_HMEBOX_EXT0_8192E + (h2c_box_num *RTL8192E_EX_MESSAGE_BOX_SIZE);
			#ifdef CONFIG_H2C_EF
			for(cmd_idx=0;cmd_idx<ext_cmd_len;cmd_idx++ ){
				rtw_write8(padapter,msgbox_ex_addr+cmd_idx,*((u8*)(&h2c_cmd_ex)+cmd_idx));
			}
			#else
			h2c_cmd_ex = le32_to_cpu( h2c_cmd_ex );
			rtw_write32(padapter, msgbox_ex_addr, h2c_cmd_ex);
			#endif
		}
		// Write command
		msgbox_addr =REG_HMEBOX_0 + (h2c_box_num *RTL8192E_MESSAGE_BOX_SIZE);
		#ifdef CONFIG_H2C_EF
		for(cmd_idx=0;cmd_idx<RTL8192E_MESSAGE_BOX_SIZE;cmd_idx++ ){
			rtw_write8(padapter,msgbox_addr+cmd_idx,*((u8*)(&h2c_cmd)+cmd_idx));
		}
		#else
		h2c_cmd = le32_to_cpu( h2c_cmd );
		rtw_write32(padapter,msgbox_addr, h2c_cmd);
		#endif

	//	DBG_8192C("MSG_BOX:%d,CmdLen(%d), reg:0x%x =>h2c_cmd:0x%x, reg:0x%x =>h2c_cmd_ex:0x%x ..\n"
	//	 	,pHalData->LastHMEBoxNum ,CmdLen,msgbox_addr,h2c_cmd,msgbox_ex_addr,h2c_cmd_ex);

		pHalData->LastHMEBoxNum = (h2c_box_num+1) % RTL8192E_MAX_H2C_BOX_NUMS;

	}while(0);

	ret = _SUCCESS;

exit:

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);	

_func_exit_;

	return ret;
}

u8 rtl8192e_h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf)
{
	u8 ElementID, CmdLen;
	u8 *pCmdBuffer;
	struct cmd_msg_parm  *pcmdmsg;

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	pcmdmsg = (struct cmd_msg_parm*)pbuf;
	ElementID = pcmdmsg->eid;
	CmdLen = pcmdmsg->sz;
	pCmdBuffer = pcmdmsg->buf;

	FillH2CCmd_8192E(padapter, ElementID, CmdLen, pCmdBuffer);

	return H2C_SUCCESS;
}

u8 rtl8192e_set_rssi_cmd(_adapter*padapter, u8 *param)
{
	u8	res=_SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
_func_enter_;

	*((u32*) param ) = cpu_to_le32( *((u32*) param ) );

	FillH2CCmd_8192E(padapter, H2C_8192E_RSSI_REPORT, 4, param);

_func_exit_;

	return res;
}

void rtl8192e_set_raid_cmd(PADAPTER padapter, u32 bitmap, u8* arg)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info	*psta;
	u8 macid, init_rate, raid, shortGIrate=_FALSE;

_func_enter_;

	macid = arg[0];
	raid = arg[1];
	shortGIrate = arg[2];
	init_rate = arg[3];

	psta = pmlmeinfo->FW_sta_info[macid].psta;
	if(psta == NULL)
	{
		return;
	}

	if(pHalData->fw_ractrl == _TRUE)
	{
		u8	H2CCommand[7] ={0};
	
		H2CCommand[0] = macid;
		H2CCommand[1] = (raid & 0x1F) | (shortGIrate?0x80:0x00) ;
		H2CCommand[2] = (psta->bw_mode & 0x3); // LDPC_CAP,DISPT,DISRA
#ifdef CONFIG_INTEL_PROXIM
		if(padapter->proximity.proxim_on ==_TRUE)
			pHalData->bDisableTXPowerTraining = _FALSE;
#endif
		//DisableTXPowerTraining
		if(pHalData->bDisableTXPowerTraining){
			H2CCommand[2] |= BIT6;
			DBG_871X("%s,Disable PWT by driver\n",__FUNCTION__);
		}
		else{
			PDM_ODM_T	pDM_OutSrc = &pHalData->odmpriv;
	
			if(pDM_OutSrc->bDisablePowerTraining){
				H2CCommand[2] |= BIT6;
				DBG_871X("%s,Disable PWT by DM\n",__FUNCTION__);	
			}
		}	

		H2CCommand[3] = (u8)(bitmap & 0x000000ff);
		H2CCommand[4] = (u8)((bitmap & 0x0000ff00) >>8);
		H2CCommand[5] = (u8)((bitmap & 0x00ff0000) >> 16);
		H2CCommand[6] = (u8)((bitmap & 0xff000000) >> 24);

		DBG_871X("%s, bitmap=0x%x, mac_id=0x%x, raid=0x%x, shortGIrate=%x\n",__FUNCTION__, bitmap, macid, raid, shortGIrate);

		FillH2CCmd_8192E(padapter, H2C_8192E_RA_MASK, 7, H2CCommand);
	}
	
	if (shortGIrate==_TRUE)
		init_rate |= BIT(7);
	
	pdmpriv->INIDATA_RATE[macid] = init_rate;
_func_exit_;

}

void rtl8192e_Add_RateATid(PADAPTER pAdapter, u32 bitmap, u8* arg, u8 rssi_level)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	macid;

	macid = arg[0];

	if(rssi_level != DM_RATR_STA_INIT)
		bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv, macid, bitmap, rssi_level);

	rtl8192e_set_raid_cmd(pAdapter, bitmap, arg);
}

void rtl8192e_set_FwPwrMode_cmd(PADAPTER padapter, u8 PSMode)
{
	u8	u1H2CSetPwrMode[H2C_PWRMODE_LEN]={0};
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8	Mode = 0, RLBM = 0, PowerState = 0, LPSAwakeIntvl = 2;
        u8   pwrModeByte5=0;
        HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

_func_enter_;

	DBG_871X("%s: Mode=%d SmartPS=%d UAPSD=%d\n", __FUNCTION__,
			PSMode, pwrpriv->smart_ps, padapter->registrypriv.uapsd_enable);

	switch(PSMode)
	{
		case PS_MODE_ACTIVE:
			Mode = 0;
			break;
		case PS_MODE_MIN:
			Mode = 1;
			break;
		case PS_MODE_MAX:
			RLBM = 1;
			Mode = 1;
			break;
		case PS_MODE_DTIM:
			RLBM = 2;
			Mode = 1;
			break;
		case PS_MODE_UAPSD_WMM:
			Mode = 2;
			break;
		default:
			Mode = 0;
			break;
	}

	if (Mode > PS_MODE_ACTIVE) {
#ifdef CONFIG_BT_COEXIST
		if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE && (pHalData->EEPROMBluetoothCoexist == 1))
		{
			PowerState = rtw_btcoex_RpwmVal(padapter);
			pwrModeByte5 = rtw_btcoex_LpsVal(padapter);

		}
		else
#endif // CONFIG_BT_COEXIST
		{
			PowerState = 0x00;// AllON(0x0C), RFON(0x04), RFOFF(0x00)
			pwrModeByte5 = 0x40;
		}
	} else {
		PowerState = 0x0C;// AllON(0x0C), RFON(0x04), RFOFF(0x00)
		pwrModeByte5 = 0x40;
	}
/*
	if(BT_1AntPowerSaveMode(Adapter) && (FW_PS_ACTIVE_MODE != Mode))
	{
		Mode = FW_PS_MIN_MODE;
	}
*/	
	// 0: Active, 1: LPS, 2: WMMPS
	SET_8192E_H2CCMD_PWRMODE_PARM_MODE(u1H2CSetPwrMode, Mode);

	// 0:Min, 1:Max , 2:User define
	SET_8192E_H2CCMD_PWRMODE_PARM_RLBM(u1H2CSetPwrMode, RLBM);

	// (LPS) smart_ps:  0: PS_Poll, 1: PS_Poll , 2: NullData
	// (WMM)smart_ps: 0:PS_Poll, 1:NullData
	SET_8192E_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CSetPwrMode, pwrpriv->smart_ps);

	// AwakeInterval: Unit is beacon interval, this field is only valid in PS_DTIM mode
	SET_8192E_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CSetPwrMode, LPSAwakeIntvl);

	// (WMM only)bAllQueueUAPSD
	SET_8192E_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CSetPwrMode, padapter->registrypriv.uapsd_enable);    
/*
	if(PSMode == FW_PS_ACTIVE_MODE)
	{
		PowerState |= FW_PWR_STATE_ACTIVE;
	}
	else
	{
		if(BT_1AntPowerSaveMode(Adapter))
		{
			PowerState = BT_1AntRpwmVal(Adapter);
			pwrModeByte5 = BT_1AntLpsVal(Adapter);
			SET_8192E_H2CCMD_PWRMODE_PARM_BYTE5(u1H2CSetPwrMode, pwrModeByte5);
		}
		else
			PowerState |= FW_PWR_STATE_RF_OFF;
	}

*/
	// AllON(0x0C), RFON(0x04), RFOFF(0x00)
	SET_8192E_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CSetPwrMode, PowerState);

        SET_8192E_H2CCMD_PWRMODE_PARM_BYTE5(u1H2CSetPwrMode, pwrModeByte5);

/*
	if(BT_1Ant(Adapter) && BT_IsBtCoexManualControl(Adapter))
		return;
	
	BT_RecordPwrMode(Adapter, &u1H2CSetPwrMode[0], 6);
*/
#ifdef CONFIG_BT_COEXIST
    if (pHalData->EEPROMBluetoothCoexist == 1)
	rtw_btcoex_RecordPwrMode(padapter, u1H2CSetPwrMode, H2C_PWRMODE_LEN);
#endif // CONFIG_BT_COEXIST

	FillH2CCmd_8192E(padapter, H2C_8192E_SETPWRMODE, sizeof(u1H2CSetPwrMode), (u8 *)&u1H2CSetPwrMode);
	//BT_DecExecPwrCmdCnt(Adapter);	
	
_func_exit_;
}

void rtl8192e_set_FwMediaStatus_cmd(PADAPTER padapter, u16 mstatus_rpt )
{
	u8 opmode,macid;
	u16 mst_rpt = cpu_to_le16 (mstatus_rpt);
	opmode = (u8) mst_rpt;
	macid = (u8)(mst_rpt >> 8)  ;
	
	DBG_871X("### %s: MStatus=%x MACID=%d \n", __FUNCTION__,opmode,macid);
	FillH2CCmd_8192E(padapter, H2C_8192E_MSRRPT, sizeof(mst_rpt), (u8 *)&mst_rpt);
}

void ConstructBeacon(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					rate_len, pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	//timestamp will be inserted by hardware
	pframe += 8;
	pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	// capability info: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
		pktlen += cur_network->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		_rtw_memcpy(pframe, cur_network->IEs+sizeof(NDIS_802_11_FIXED_IEs), pktlen);

		goto _ConstructBeacon;
	}

	//below for ad-hoc mode

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	//todo: ERP IE


	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);
	}


	//todo:HT for adhoc

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512)
	{
		DBG_871X("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	//DBG_871X("%s bcn_sz=%d\n", __FUNCTION__, pktlen);

}

void ConstructPSPoll(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	// Frame control.
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	// AID.
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	// BSSID.
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	// TA.
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	*pLength = 16;
}

void ConstructNullFunctionData(
	PADAPTER padapter,
	u8		*pframe,
	u32		*pLength,
	u8		*StaAddr,
	u8		bQoS,
	u8		AC,
	u8		bEosp,
	u8		bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave)
	{
		SetPwrMgt(fctrl);
	}

	switch(cur_network->network.InfrastructureMode)
	{
		case Ndis802_11Infrastructure:
			SetToDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
			break;
		case Ndis802_11APMode:
			SetFrDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);
			break;
		case Ndis802_11IBSS:
		default:
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			break;
	}

	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE) {
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos*)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

void ConstructProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bHideSSID)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u8					*mac, *bssid;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD
#endif //CONFIG_P2P


	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	DBG_871X("%s FW Mac Addr:" MAC_FMT "\n", __FUNCTION__, MAC_ARG(mac));
	DBG_871X("%s FW IP Addr" IP_FMT "\n", __FUNCTION__, IP_ARG(StaAddr));

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	if(cur_network->IELength>MAX_IE_SZ)
		return;

	pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_,
			cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

	//inerset & update wps_probe_resp_ie
	if ((pmlmepriv->wps_probe_resp_ie!=NULL) && pwps_ie && (wps_ielen>0)) {
		uint wps_offset, remainder_ielen;
		u8 *premainder_ie;

		wps_offset = (uint)(pwps_ie - cur_network->IEs);

		premainder_ie = pwps_ie + wps_ielen;

		remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

		_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
		pframe += wps_offset;
		pktlen += wps_offset;

		wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];//to get ie data len
		if ((wps_offset+wps_ielen+2)<=MAX_IE_SZ) {
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
			pframe += wps_ielen+2;
			pktlen += wps_ielen+2;
		}

		if ((wps_offset+wps_ielen+2+remainder_ielen)<=MAX_IE_SZ) {
			_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
			pframe += remainder_ielen;
			pktlen += remainder_ielen;
		}
	} else {
		_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
		pframe += cur_network->IELength;
		pktlen += cur_network->IELength;
	}

	/* retrieve SSID IE from cur_network->Ssid */
	{
		u8 *ssid_ie;
		sint ssid_ielen = 0;
		sint ssid_ielen_diff;
		u8 buf[MAX_IE_SZ];
		u8 *ies = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);

		ssid_ie = rtw_get_ie(ies+_FIXED_IE_LENGTH_, _SSID_IE_, &ssid_ielen,
				(pframe-ies)-_FIXED_IE_LENGTH_);

		ssid_ielen_diff = cur_network->Ssid.SsidLength - ssid_ielen;

		if (ssid_ie &&  cur_network->Ssid.SsidLength) {
			uint remainder_ielen;
			u8 *remainder_ie;
			remainder_ie = ssid_ie+2;
			remainder_ielen = (pframe-remainder_ie);

			if (remainder_ielen > MAX_IE_SZ) {
				DBG_871X_LEVEL(_drv_warning_, FUNC_ADPT_FMT" remainder_ielen > MAX_IE_SZ\n", FUNC_ADPT_ARG(padapter));
				remainder_ielen = MAX_IE_SZ;
			}

			_rtw_memcpy(buf, remainder_ie, remainder_ielen);
			_rtw_memcpy(remainder_ie+ssid_ielen_diff, buf, remainder_ielen);
			*(ssid_ie+1) = cur_network->Ssid.SsidLength;
			_rtw_memcpy(ssid_ie+2, cur_network->Ssid.Ssid, cur_network->Ssid.SsidLength);
			pframe += ssid_ielen_diff;
			pktlen += ssid_ielen_diff;
		}
	}

#ifdef CONFIG_P2P
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) /*&& is_valid_p2p_probereq*/)
	{
		u32 len;
#ifdef CONFIG_IOCTL_CFG80211
		if(adapter_wdev_data(padapter)->p2p_enabled && pwdinfo->driver_interface == DRIVER_CFG80211 )
		{
			//if pwdinfo->role == P2P_ROLE_DEVICE will call issue_probersp_p2p()
			len = pmlmepriv->p2p_go_probe_resp_ie_len;
			if(pmlmepriv->p2p_go_probe_resp_ie && len>0)
				_rtw_memcpy(pframe, pmlmepriv->p2p_go_probe_resp_ie, len);
		}
		else
#endif //CONFIG_IOCTL_CFG80211
		{
			len = build_probe_resp_p2p_ie(pwdinfo, pframe);
		}

		pframe += len;
		pktlen += len;
		
#ifdef CONFIG_WFD
#ifdef CONFIG_IOCTL_CFG80211
		if(_TRUE == pwdinfo->wfd_info->wfd_enable)
#endif //CONFIG_IOCTL_CFG80211
		{
			len = build_probe_resp_wfd_ie(pwdinfo, pframe, 0);
		}
#ifdef CONFIG_IOCTL_CFG80211
		else
		{	
			len = 0;
			if(pmlmepriv->wfd_probe_resp_ie && pmlmepriv->wfd_probe_resp_ie_len>0)
			{
				len = pmlmepriv->wfd_probe_resp_ie_len;
				_rtw_memcpy(pframe, pmlmepriv->wfd_probe_resp_ie, len);	
			}	
		}
#endif //CONFIG_IOCTL_CFG80211		
		pframe += len;
		pktlen += len;
#endif //CONFIG_WFD

	}
#endif //CONFIG_P2P

	*pLength = pktlen;
}

// To check if reserved page content is destroyed by beacon beacuse beacon is too large.
// 2010.06.23. Added by tynli.
VOID
CheckFwRsvdPageContent(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);
	u32	MaxBcnPageNum;

 	if(pHalData->FwRsvdPageStartOffset != 0)
 	{
 		/*MaxBcnPageNum = PageNum_128(pMgntInfo->MaxBeaconSize);
		RT_ASSERT((MaxBcnPageNum <= pHalData->FwRsvdPageStartOffset),
			("CheckFwRsvdPageContent(): The reserved page content has been"\
			"destroyed by beacon!!! MaxBcnPageNum(%d) FwRsvdPageStartOffset(%d)\n!",
			MaxBcnPageNum, pHalData->FwRsvdPageStartOffset));*/
 	}
}

//
// Description: Get the reserved page number in Tx packet buffer.
// Retrun value: the page number.
// 2012.08.09, by tynli.
//
u8
GetTxBufferRsvdPageNum8192E(_adapter *padapter, bool wowlan)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	RsvdPageNum=0;
	// default reseved 1 page for the IC type which is undefined.
	u8	TxPageBndy= LAST_ENTRY_OF_TX_PKT_BUFFER_8192C;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&TxPageBndy);

	RsvdPageNum = LAST_ENTRY_OF_TX_PKT_BUFFER_8192C -TxPageBndy + 1;

	return RsvdPageNum;
}

//
// Description: Fill the reserved packets that FW will use to RSVD page.
//			Now we just send 4 types packet to rsvd page.
//			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
//	Input:
//	    bDLFinished - FALSE: At the first time we will send all the packets as a large packet to Hw,
//				 		so we need to set the packet length to total lengh.
//			      TRUE: At the second time, we should send the first packet (default:beacon)
//						to Hw again and set the lengh in descriptor to the real beacon lengh.
// 2009.10.15 by tynli.



static void SetFwRsvdPagePkt(PADAPTER padapter, BOOLEAN bDLFinished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	u32	BeaconLength = 0, ProbeRspLength = 0, PSPollLength = 0;
	u32	NullDataLength = 0, QosNullLength = 0, BTQosNullLength = 0;
	u8	*ReservedPagePacket;
	u8	PageNum, PageNeed, TxDescLen;
	u16	BufIndex;
	u32	TotalPacketLen;
	RSVDPAGE_LOC	RsvdPageLoc;


	DBG_871X("%s\n", __FUNCTION__);

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;

	TxDescLen = TXDESC_SIZE;
	PageNum = 0;

	//3 (1) beacon * 2 pages, to do DL BCNx4 pages
	BufIndex = TXDESC_OFFSET;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	PageNeed = (u8)PageNum_256(TxDescLen + BeaconLength);
	// To reserved 4 pages for beacon buffer. 2010.06.24.
	if (PageNeed == 1)
		PageNeed += 1;
#ifdef CONFIG_2BCN_EN
	PageNeed += 2;
#endif
	PageNum += PageNeed;	
	
	pHalData->FwRsvdPageStartOffset = PageNum;
	//printk("%s => BCN#1 PageNeed(%d) FwRsvdPageStartOffset(%d) \n",__FUNCTION__, PageNeed,pHalData->FwRsvdPageStartOffset);
	
	BufIndex += PageNeed*PAGE_SIZE_TX_92E;

	//3 (2) ps-poll *1 page
	RsvdPageLoc.LocPsPoll = PageNum;
	ConstructPSPoll(padapter, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, _TRUE, _FALSE, _FALSE);

	PageNeed = (u8)PageNum_256(TxDescLen + PSPollLength);
	PageNum += PageNeed;
	//printk("%s => PS-POLL- PageNeed(%d)\n",__FUNCTION__, PageNeed);
	BufIndex += PageNeed*PAGE_SIZE_TX_92E;

	//3 (3) null data * 1 page
	RsvdPageLoc.LocNullData = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&NullDataLength,
		get_my_bssid(&pmlmeinfo->network),
		_FALSE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, _FALSE, _FALSE, _FALSE);

	PageNeed = (u8)PageNum_256(TxDescLen + NullDataLength);
	PageNum += PageNeed;
	//printk("%s => NULL data - PageNeed(%d)\n",__FUNCTION__, PageNeed);
	BufIndex += PageNeed*PAGE_SIZE_TX_92E;

	//3 (4) probe response * 1page
	RsvdPageLoc.LocProbeRsp = PageNum;
	ConstructProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		get_my_bssid(&pmlmeinfo->network),
		_FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, _FALSE, _FALSE, _FALSE);

	PageNeed = (u8)PageNum_256(TxDescLen + ProbeRspLength);
	PageNum += PageNeed;
	//printk("%s => PROB-RSP - PageNeed(%d)\n",__FUNCTION__, PageNeed);
	BufIndex += PageNeed*PAGE_SIZE_TX_92E;

	//3 (5) Qos null data
	RsvdPageLoc.LocQosNull = PageNum;
	ConstructNullFunctionData(
		padapter, 
		&ReservedPagePacket[BufIndex],
		&QosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, _FALSE, _FALSE, _FALSE);

	PageNeed = (u8)PageNum_256(TxDescLen + QosNullLength);
	PageNum += PageNeed;
	//printk("%s => QOS-NULL - PageNeed(%d)\n",__FUNCTION__, PageNeed);


	TotalPacketLen = BufIndex + QosNullLength;

/*
	BufIndex += PageNeed*256;

	//3 (6) BT Qos null data
	RsvdPageLoc.LocBTQosNull = PageNum;
	ConstructNullFunctionData(
		padapter, 
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, _FALSE, _TRUE);

	TotalPacketLen = BufIndex + BTQosNullLength;
*/

	if(TotalPacketLen > RSVD_PKT_LEN_92E)
	{
		DBG_871X("%s: error => TotalPacketLen(%d) > RSVD_PKT_LEN_92E(%d) \n", __FUNCTION__
			,TotalPacketLen,RSVD_PKT_LEN_92E);
		goto error;
	}

	// update attribute
	pattrib = &pcmdframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TXDESC_OFFSET;

#ifdef CONFIG_PCI_HCI
	dump_mgntframe(padapter, pcmdframe);
#else
	dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif

	DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), PageNum(%d)\n", __FUNCTION__,TotalPacketLen,PageNum);

	FillH2CCmd_8192E(padapter, H2C_8192E_RSVDPAGE, H2C_RSVDPAGE_LOC_LEN, (u8*)&RsvdPageLoc);

	return;

error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

#ifdef CONFIG_AP_WOWLAN
static void rtl8192e_set_ap_wow_rsvdpage_cmd(PADAPTER padapter,
		PRSVDPAGE_LOC rsvdpageloc)
{
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = 0, count = 0, header = 0;
	u8 rsvdparm[H2C_AOAC_RSVDPAGE_LOC_LEN]={0};

	header = rtw_read8(padapter, REG_BCNQ_BDNY);

	DBG_871X("%s: beacon: %d, probeRsp: %d, header:0x%02x\n", __func__,
			rsvdpageloc->LocApOffloadBCN,
			rsvdpageloc->LocProbeRsp,
			header);

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_BCN(rsvdparm,
			rsvdpageloc->LocApOffloadBCN + header);

	FillH2CCmd_8192E(padapter, H2C_8192E_BCN_RSVDPAGE,
			H2C_BCN_RSVDPAGE_LEN, rsvdparm);

	rtw_msleep_os(10);

	_rtw_memset(&rsvdparm, 0, sizeof(rsvdparm));

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_ProbeRsp(
			rsvdparm,
			rsvdpageloc->LocProbeRsp + header);

	FillH2CCmd_8192E(padapter, H2C_8192E_PROBERSP_RSVDPAGE,
			H2C_PROBERSP_RSVDPAGE_LEN, rsvdparm);

	rtw_msleep_os(10);
}


//
//Description: Fill the reserved packets that FW will use to RSVD page.
//Now we just send 2 types packet to rsvd page. (1)Beacon, (2)ProbeRsp.
//
//Input: bDLFinished	
//
//FALSE: At the first time we will send all the packets as a large packet to Hw,
//	 so we need to set the packet length to total lengh.
//
//TRUE: At the second time, we should send the first packet (default:beacon)
//	to Hw again and set the lengh in descriptor to the real beacon lengh.
// 2009.10.15 by tynli.
static void rtl8192e_set_AP_FwRsvdPagePkt(PADAPTER padapter,
		BOOLEAN bDLFinished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u32	BeaconLength=0, ProbeRspLength=0;
	u8	*ReservedPagePacket;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum=0, CurtPktPageNum=0, RsvdPageNum=0;
	u8	currentip[4];
	u16	BufIndex, PageSize = PAGE_SIZE_TX_92E;
	u32	TotalPacketLen = 0, MaxRsvdPageBufSize=0;
	RSVDPAGE_LOC	RsvdPageLoc;
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif // DBG_CONFIG_ERROR_DETECT

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d\n",
	FUNC_ADPT_ARG(padapter), get_iface_type(padapter));

	pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	psrtpriv = &pHalData->srestpriv;
#endif
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);
	RsvdPageNum = TOTAL_RSVD_PAGE_NUMBER_8192E;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	//3 (1) beacon
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	CurtPktPageNum = (u8)PageNum_256(TxDescLen + BeaconLength);
	//If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware
	if (CurtPktPageNum == 1)
	{
		CurtPktPageNum += 1;
	}
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//2 (4) probe response
	RsvdPageLoc.LocProbeRsp = TotalPageNum;
	rtw_get_current_ip_address(padapter, currentip);
	ConstructProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		currentip,
		_FALSE);
	rtl8192e_fill_fake_txdesc(padapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			ProbeRspLength,
			_FALSE, _FALSE);

	DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n",
		__func__, &ReservedPagePacket[BufIndex-TxDescLen],
		(ProbeRspLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + ProbeRspLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	TotalPacketLen = BufIndex + ProbeRspLength;

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(): ERROR: The rsvd page size is not enough \
				!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
				__func__, TotalPacketLen,MaxRsvdPageBufSize);
		goto error;
	} else {
		// update attribute
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = TotalPacketLen - TxDescOffset;
		pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
		dump_mgntframe(padapter, pcmdframe);
#else
		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif
	}

	DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n", __FUNCTION__,TotalPacketLen,TotalPageNum);
	rtl8192e_set_ap_wow_rsvdpage_cmd(padapter, &RsvdPageLoc);

	return;
error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}
#endif //CONFIG_AP_WOWLAN

void rtl8192e_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus)
{
	JOINBSSRPT_PARM_92E	JoinBssRptParm;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	BOOLEAN		bSendBeacon=_FALSE;
	BOOLEAN		bcn_valid = _FALSE;
	u8	DLBcnCount=0;
	u32 poll = 0;

_func_enter_;

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d mstatus(%x)\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), mstatus);

	if(mstatus == 1)
	{
		// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		// Suggested by filen. Added by tynli.
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		// Do not set TSF again here or vWiFi beacon DMA INT will not work.
		//correct_TSF(padapter, pmlmeext);
		// Hw sequende enable by dedault. 2010.06.23. by tynli.
		//rtw_write16(padapter, REG_NQOS_SEQ, ((pmlmeext->mgnt_seq+100)&0xFFF));
		//rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);

		//Set REG_CR bit 8. DMA beacon by SW.
		pHalData->RegCR_1 |= BIT0;
		rtw_write8(padapter,  REG_CR+1, pHalData->RegCR_1);
		
		// Disable Hw protection for a time which revserd for Hw sending beacon.
		// Fix download reserved page packet fail that access collision with the protection time.
		// 2010.05.11. Added by tynli.
		//SetBcnCtrlReg(padapter, 0, BIT3);
		//SetBcnCtrlReg(padapter, BIT4, 0);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~EN_BCN_FUNCTION));
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|DIS_TSF_UDT);
			
		if(pHalData->RegFwHwTxQCtrl&BIT6)
		{
			DBG_871X("HalDownloadRSVDPage(): There is an Adapter is sending beacon.\n");
			bSendBeacon = _TRUE;
		}

		// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT6)));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);

		// Clear beacon valid check bit.
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
		DLBcnCount = 0;
		poll = 0;
		do
		{
#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode)
				rtl8192e_set_AP_FwRsvdPagePkt(padapter, 0);
			else
				rtw_hal_set_fw_rsvd_page(padapter, 0);
#else
			// download rsvd page.
			rtw_hal_set_fw_rsvd_page(padapter, _FALSE);
#endif
			DLBcnCount++;
			do
			{
				rtw_yield_os();
				//rtw_mdelay_os(10);
				// check rsvd page download OK.
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8*)(&bcn_valid));
				poll++;
			} while(!bcn_valid && (poll%10)!=0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
			
		}while(!bcn_valid && DLBcnCount<=100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
		
		//RT_ASSERT(bcn_valid, ("HalDownloadRSVDPage88ES(): 1 Download RSVD page failed!\n"));
		if(padapter->bSurpriseRemoved || padapter->bDriverStopped)
		{
		}
		else if(!bcn_valid)
			DBG_871X(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter) ,DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			DBG_871X(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		}
		//
		// We just can send the reserved page twice during the time that Tx thread is stopped (e.g. pnpsetpower)
		// becuase we need to free the Tx BCN Desc which is used by the first reserved page packet.
		// At run time, we cannot get the Tx Desc until it is released in TxHandleInterrupt() so we will return
		// the beacon TCB in the following code. 2011.11.23. by tynli.
		//
		//if(bcn_valid && padapter->bEnterPnpSleep)
		if(0)
		{
			if(bSendBeacon)
			{
				rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
				rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
				DLBcnCount = 0;
				poll = 0;
				do
				{
					SetFwRsvdPagePkt(padapter, _TRUE);
					DLBcnCount++;
					
					do
					{
						rtw_yield_os();
						//rtw_mdelay_os(10);
						// check rsvd page download OK.
						rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8*)(&bcn_valid));
						poll++;
					} while(!bcn_valid && (poll%10)!=0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
				}while(!bcn_valid && DLBcnCount<=100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
				
				//RT_ASSERT(bcn_valid, ("HalDownloadRSVDPage(): 2 Download RSVD page failed!\n"));
				if(padapter->bSurpriseRemoved || padapter->bDriverStopped)
				{
				}
				else if(!bcn_valid)
					DBG_871X("%s: 2 Download RSVD page failed! DLBcnCount:%u, poll:%u\n", __FUNCTION__ ,DLBcnCount, poll);
				else
					DBG_871X("%s: 2 Download RSVD success! DLBcnCount:%u, poll:%u\n", __FUNCTION__, DLBcnCount, poll);
			}
		}

		// Enable Bcn
		//SetBcnCtrlReg(padapter, BIT3, 0);
		//SetBcnCtrlReg(padapter, 0, BIT4);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|EN_BCN_FUNCTION);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));

		// To make sure that if there exists an adapter which would like to send beacon.
		// If exists, the origianl value of 0x422[6] will be 1, we should check this to
		// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause 
		// the beacon cannot be sent by HW.
		// 2010.06.23. Added by tynli.
		if(bSendBeacon)
		{
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl|BIT6));
			pHalData->RegFwHwTxQCtrl |= BIT6;
		}

		//
		// Update RSVD page location H2C to Fw.
		//
		if(bcn_valid)
		{
			rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
			DBG_871X("Set RSVD page location to Fw.\n");
			//FillH2CCmd88E(Adapter, H2C_88E_RSVDPAGE, H2C_RSVDPAGE_LOC_LENGTH, pMgntInfo->u1RsvdPageLoc);
		}
		
		// Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.
		//if(!padapter->bEnterPnpSleep)
		{
			// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
			pHalData->RegCR_1 &= (~BIT0);
			rtw_write8(padapter,  REG_CR+1, pHalData->RegCR_1);
		}
	}
#ifdef CONFIG_WOWLAN
	if (adapter_to_pwrctl(padapter)->wowlan_mode){
		JoinBssRptParm.OpMode = mstatus;
		JoinBssRptParm.MacID = 0;
		FillH2CCmd_8192E(padapter, H2C_8192E_MSRRPT, sizeof(JoinBssRptParm), (u8 *)&JoinBssRptParm);
		DBG_871X_LEVEL(_drv_info_, "%s opmode:%d MacId:%d\n", __func__, JoinBssRptParm.OpMode, JoinBssRptParm.MacID);
	} else {
		DBG_871X_LEVEL(_drv_info_, "%s wowlan_mode is off\n", __func__);
	}
#endif //CONFIG_WOWLAN
_func_exit_;
}

#ifdef CONFIG_P2P_PS
void rtl8192e_set_p2p_ctw_period_cmd(_adapter* padapter, u8 ctwindow)
{
	struct P2P_PS_CTWPeriod_t p2p_ps_ctw;

	p2p_ps_ctw.CTWPeriod = ctwindow;

	FillH2CCmd_8192E(padapter, H2C_8192E_P2P_PS_OFFLOAD, 1, (u8 *)(&p2p_ps_ctw));

}

void rtl8192e_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrpriv = adapter_to_pwrctl(padapter);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8	*p2p_ps_offload = &pHalData->p2p_ps_offload;
		
	u8	i;

_func_enter_;

#if 1
	switch(p2p_ps_state)
	{
		case P2P_PS_DISABLE:
			DBG_8192C("P2P_PS_DISABLE \n");
			_rtw_memset(p2p_ps_offload, 0, 1);
			break;
		case P2P_PS_ENABLE:
			DBG_8192C("P2P_PS_ENABLE \n");
			// update CTWindow value.
			if( pwdinfo->ctwindow > 0 )
			{
				SET_8192E_H2CCMD_P2P_PS_OFFLOAD_CTWINDOW_EN(p2p_ps_offload, 1);
				rtw_write8(padapter, REG_P2P_CTWIN, pwdinfo->ctwindow);				
			}

			// hw only support 2 set of NoA
			for( i=0 ; i<pwdinfo->noa_num ; i++)
			{
				// To control the register setting for which NOA
				rtw_write8(padapter, REG_NOA_DESC_SEL, (i << 4));
				if(i == 0) {
					SET_8192E_H2CCMD_P2P_PS_OFFLOAD_NOA0_EN(p2p_ps_offload, 1);
				} else {
					SET_8192E_H2CCMD_P2P_PS_OFFLOAD_NOA1_EN(p2p_ps_offload, 1);
				}

				// config P2P NoA Descriptor Register
				//DBG_8192C("%s(): noa_duration = %x\n",__FUNCTION__,pwdinfo->noa_duration[i]);
				rtw_write32(padapter, REG_NOA_DESC_DURATION, pwdinfo->noa_duration[i]);

				//DBG_8192C("%s(): noa_interval = %x\n",__FUNCTION__,pwdinfo->noa_interval[i]);
				rtw_write32(padapter, REG_NOA_DESC_INTERVAL, pwdinfo->noa_interval[i]);

				//DBG_8192C("%s(): start_time = %x\n",__FUNCTION__,pwdinfo->noa_start_time[i]);
				rtw_write32(padapter, REG_NOA_DESC_START, pwdinfo->noa_start_time[i]);

				//DBG_8192C("%s(): noa_count = %x\n",__FUNCTION__,pwdinfo->noa_count[i]);
				rtw_write8(padapter, REG_NOA_DESC_COUNT, pwdinfo->noa_count[i]);
			}

			if( (pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0) )
			{
				// rst p2p circuit
				rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(4));

				SET_8192E_H2CCMD_P2P_PS_OFFLOAD_ENABLE(p2p_ps_offload, 1);

				if(pwdinfo->role == P2P_ROLE_GO)
				{
					// 1: Owner, 0: Client
					SET_8192E_H2CCMD_P2P_PS_OFFLOAD_ROLE(p2p_ps_offload, 1);
					SET_8192E_H2CCMD_P2P_PS_OFFLOAD_ALLSTASLEEP(p2p_ps_offload, 0);
				}
				else
				{
					// 1: Owner, 0: Client
					SET_8192E_H2CCMD_P2P_PS_OFFLOAD_ROLE(p2p_ps_offload, 0);
				}

				//SET_8192E_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(p2p_ps_offload, 0);
			}
			break;
		case P2P_PS_SCAN:
			DBG_8192C("P2P_PS_SCAN \n");
			//SET_8192E_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(p2p_ps_offload, 1);
			break;
		case P2P_PS_SCAN_DONE:
			DBG_8192C("P2P_PS_SCAN_DONE \n");
			//SET_8192E_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(p2p_ps_offload, 0);
			pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
			break;
		default:
			break;
	}

	DBG_871X("P2P_PS_OFFLOAD : %x\n", p2p_ps_offload[0]);
	FillH2CCmd_8192E(padapter, H2C_8192E_P2P_PS_OFFLOAD, 1, p2p_ps_offload);
#endif

_func_exit_;

}

#endif //CONFIG_P2P

#ifdef CONFIG_TSF_RESET_OFFLOAD
/*
	ask FW to Reset sync register at Beacon early interrupt
*/
u8 rtl8192e_reset_tsf(_adapter *padapter, u8 reset_port )
{	
	u8	buf[2];
	u8	res=_SUCCESS;

	s32 ret;
_func_enter_;
	if (IFACE_PORT0==reset_port) {
		buf[0] = 0x1; buf[1] = 0;
	} else{
		buf[0] = 0x0; buf[1] = 0x1;
	}

	ret = FillH2CCmd_8192E(padapter, H2C_RESET_TSF, 2, buf);

_func_exit_;

	return res;
}

int reset_tsf(PADAPTER Adapter, u8 reset_port )
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (IFACE_PORT0==reset_port) ?
				REG_FW_RESET_TSF_CNT_0:REG_FW_RESET_TSF_CNT_1;
	u32 reg_bcncrtl = (IFACE_PORT0==reset_port) ?
				REG_BCN_CTRL_1:REG_BCN_CTRL;

	rtw_scan_abort(Adapter->pbuddy_adapter);	/*	site survey will cause reset_tsf fail	*/
	reset_cnt_after = reset_cnt_before = rtw_read8(Adapter,reg_reset_tsf_cnt);
	rtl8192e_reset_tsf(Adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before ) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(Adapter, reg_reset_tsf_cnt);
	}

	return(loop_cnt >= 10) ? _FAIL : _TRUE;
}


#endif	// CONFIG_TSF_RESET_OFFLOAD

static void rtl8192e_set_FwRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	u8 u1H2CRsvdPageParm[H2C_RSVDPAGE_LOC_LEN]={0};

	DBG_871X("8192ERsvdPageLoc: ProbeRsp=%d PsPoll=%d Null=%d QoSNull=%d BTNull=%d\n",  
		rsvdpageloc->LocProbeRsp, rsvdpageloc->LocPsPoll,
		rsvdpageloc->LocNullData, rsvdpageloc->LocQosNull,
		rsvdpageloc->LocBTQosNull);

	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1H2CRsvdPageParm, rsvdpageloc->LocProbeRsp);
	SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1H2CRsvdPageParm, rsvdpageloc->LocPsPoll);
	SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocNullData);
	SET_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocQosNull);
	SET_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocBTQosNull);
	
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CRsvdPageParm:", u1H2CRsvdPageParm, H2C_RSVDPAGE_LOC_LEN);
	FillH2CCmd_8192E(padapter, H2C_RSVD_PAGE, H2C_RSVDPAGE_LOC_LEN, u1H2CRsvdPageParm);
}

static void rtl8192e_set_FwAoacRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = 0, count = 0;
#ifdef CONFIG_WOWLAN	
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN]={0};

	DBG_871X("8192EAOACRsvdPageLoc: RWC=%d ArpRsp=%d NbrAdv=%d GtkRsp=%d GtkInfo=%d ProbeReq=%d NetworkList=%d\n",  
			rsvdpageloc->LocRemoteCtrlInfo, rsvdpageloc->LocArpRsp,
			rsvdpageloc->LocNbrAdv, rsvdpageloc->LocGTKRsp,
			rsvdpageloc->LocGTKInfo, rsvdpageloc->LocProbeReq,
			rsvdpageloc->LocNetList);

#ifdef CONFIG_PNO_SUPPORT
	DBG_871X("NLO_INFO=%d\n", rsvdpageloc->LocPNOInfo);
#endif
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
	//SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm, rsvdpageloc->LocNbrAdv);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKRsp);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKInfo);
#ifdef CONFIG_GTK_OL
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_EXT_MEM(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKEXTMEM);
#endif // CONFIG_GTK_OL
	} else {
#ifdef CONFIG_PNO_SUPPORT
		if(!pwrpriv->pno_in_resume) {
			SET_H2CCMD_AOAC_RSVDPAGE_LOC_NLO_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocPNOInfo);
		}
#endif
	}

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CAoacRsvdPageParm:", u1H2CAoacRsvdPageParm, H2C_AOAC_RSVDPAGE_LOC_LEN);
	FillH2CCmd_8192E(padapter, H2C_AOAC_RSVD_PAGE, H2C_AOAC_RSVDPAGE_LOC_LEN, u1H2CAoacRsvdPageParm);

#ifdef CONFIG_PNO_SUPPORT
	if (!check_fwstate(pmlmepriv, WIFI_AP_STATE) &&
			!check_fwstate(pmlmepriv, _FW_LINKED) &&
			pwrpriv->pno_in_resume == _FALSE) {

		res = rtw_read8(padapter, 0x1b8);
		while(res == 0 && count < 25) {
			DBG_871X("[%d] FW loc_NLOInfo: %d\n", count, res);
			res = rtw_read8(padapter, 0x1b8);
			count++;
			rtw_msleep_os(2);
		}
	}
#endif // CONFIG_PNO_SUPPORT
#endif // CONFIG_WOWLAN
}

#ifdef CONFIG_WOWLAN

//
// Description:
//	Construct the ARP response packet to support ARP offload.
//
static void ConstructARPResponse(
	PADAPTER padapter,
	u8			*pframe,
	u32			*pLength,
	u8			*pIPAddress
	)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	static u8			ARPLLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x06};
	u8				*pARPRspPkt = pframe;
	//for TKIP Cal MIC
	u8				*payload = pframe;
	u8			EncryptionHeadOverhead = 0;
	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	//-------------------------------------------------------------------------
	// MAC Header.
	//-------------------------------------------------------------------------
	SetFrameType(fctrl, WIFI_DATA);
	//SetFrameSubType(fctrl, 0);
	SetToDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);
	//SET_80211_HDR_FRAME_CONTROL(pARPRspPkt, 0);
	//SET_80211_HDR_TYPE_AND_SUBTYPE(pARPRspPkt, Type_Data);
	//SET_80211_HDR_TO_DS(pARPRspPkt, 1);
	//SET_80211_HDR_ADDRESS1(pARPRspPkt, pMgntInfo->Bssid);
	//SET_80211_HDR_ADDRESS2(pARPRspPkt, Adapter->CurrentAddress);
	//SET_80211_HDR_ADDRESS3(pARPRspPkt, pMgntInfo->Bssid);

	//SET_80211_HDR_DURATION(pARPRspPkt, 0);
	//SET_80211_HDR_FRAGMENT_SEQUENCE(pARPRspPkt, 0);
#ifdef CONFIG_WAPI_SUPPORT
 	*pLength = sMacHdrLng;
#else
	*pLength = 24;
#endif

//YJ,del,120503
#if 0
	//-------------------------------------------------------------------------
	// Qos Header: leave space for it if necessary.
	//-------------------------------------------------------------------------
	if(pStaQos->CurrentQosMode > QOS_DISABLE)
	{
		SET_80211_HDR_QOS_EN(pARPRspPkt, 1);
		PlatformZeroMemory(&(Buffer[*pLength]), sQoSCtlLng);
		*pLength += sQoSCtlLng;
	}
#endif
	//-------------------------------------------------------------------------
	// Security Header: leave space for it if necessary.
	//-------------------------------------------------------------------------

#if 1
	switch (psecuritypriv->dot11PrivacyAlgrthm)
	{
		case _WEP40_:
		case _WEP104_:
			EncryptionHeadOverhead = 4;
			break;
		case _TKIP_:
			EncryptionHeadOverhead = 8;	
			break;			
		case _AES_:
			EncryptionHeadOverhead = 8;
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			EncryptionHeadOverhead = 18;
			break;
#endif			
		default:
			EncryptionHeadOverhead = 0;
	}
	
	if(EncryptionHeadOverhead > 0)
	{
		_rtw_memset(&(pframe[*pLength]), 0,EncryptionHeadOverhead);
	       	*pLength += EncryptionHeadOverhead;
		//SET_80211_HDR_WEP(pARPRspPkt, 1);  //Suggested by CCW.
		SetPrivacy(fctrl);
	}	
#endif
	//-------------------------------------------------------------------------
	// Frame Body.
	//-------------------------------------------------------------------------
	pARPRspPkt =  (u8*)(pframe+ *pLength);
	payload = pARPRspPkt; //Get Payload pointer
	// LLC header
	_rtw_memcpy(pARPRspPkt, ARPLLCHeader, 8);	
	*pLength += 8;

	// ARP element
	pARPRspPkt += 8;
	SET_ARP_PKT_HW(pARPRspPkt, 0x0100);
	SET_ARP_PKT_PROTOCOL(pARPRspPkt, 0x0008);	// IP protocol
	SET_ARP_PKT_HW_ADDR_LEN(pARPRspPkt, 6);
	SET_ARP_PKT_PROTOCOL_ADDR_LEN(pARPRspPkt, 4);
	SET_ARP_PKT_OPERATION(pARPRspPkt, 0x0200); // ARP response
	SET_ARP_PKT_SENDER_MAC_ADDR(pARPRspPkt, adapter_mac_addr(padapter));
	SET_ARP_PKT_SENDER_IP_ADDR(pARPRspPkt, pIPAddress);
#ifdef CONFIG_ARP_KEEP_ALIVE
	if (rtw_gw_addr_query(padapter)==0) {
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt, pmlmepriv->gw_mac_addr);
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt, pmlmepriv->gw_ip);
	}
	else
#endif
	{
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt, get_my_bssid(&(pmlmeinfo->network)));
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt, pIPAddress);
		DBG_871X("%s Target Mac Addr:" MAC_FMT "\n", __FUNCTION__, MAC_ARG(get_my_bssid(&(pmlmeinfo->network))));
		DBG_871X("%s Target IP Addr" IP_FMT "\n", __FUNCTION__, IP_ARG(pIPAddress));
	}
	
	*pLength += 28;

	if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_)
	{
		u8	mic[8];
		struct mic_data	micdata;
		struct sta_info	*psta = NULL;
		u8	priority[4]={0x0,0x0,0x0,0x0};
		u8	null_key[16]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};

		DBG_871X("%s(): Add MIC\n",__FUNCTION__);

		psta = rtw_get_stainfo(&padapter->stapriv, get_my_bssid(&(pmlmeinfo->network)));
		if (psta != NULL) {
			if(_rtw_memcmp(&psta->dot11tkiptxmickey.skey[0],null_key, 16)==_TRUE){
				DBG_871X("%s(): STA dot11tkiptxmickey==0\n",__FUNCTION__);
			}
			//start to calculate the mic code
			rtw_secmicsetkey(&micdata, &psta->dot11tkiptxmickey.skey[0]);
		}

		rtw_secmicappend(&micdata, pwlanhdr->addr3, 6);  //DA

		rtw_secmicappend(&micdata, pwlanhdr->addr2, 6); //SA

		priority[0]=0;
		rtw_secmicappend(&micdata, &priority[0], 4);

		rtw_secmicappend(&micdata, payload, 36); //payload length = 8 + 28

		rtw_secgetmic(&micdata,&(mic[0]));

		pARPRspPkt += 28;
		_rtw_memcpy(pARPRspPkt, &(mic[0]),8);

		*pLength += 8;
	}
}
#ifdef CONFIG_GTK_OL
static void ConstructGTKResponse(
	PADAPTER padapter,
	u8			*pframe,
	u32			*pLength
	)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	static u8			LLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
	static u8			GTKbody_a[11] ={0x01, 0x03, 0x00, 0x5F, 0x02, 0x03, 0x12, 0x00, 0x10, 0x42, 0x0B};
	u8				*pGTKRspPkt = pframe;
	u8			EncryptionHeadOverhead = 0;
	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	//-------------------------------------------------------------------------
	// MAC Header.
	//-------------------------------------------------------------------------
	SetFrameType(fctrl, WIFI_DATA);
	//SetFrameSubType(fctrl, 0);
	SetToDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);

#ifdef CONFIG_WAPI_SUPPORT
 	*pLength = sMacHdrLng;
#else
	*pLength = 24;
#endif //CONFIG_WAPI_SUPPORT

//YJ,del,120503
#if 0
	//-------------------------------------------------------------------------
	// Qos Header: leave space for it if necessary.
	//-------------------------------------------------------------------------
	if(pStaQos->CurrentQosMode > QOS_DISABLE)
	{
		SET_80211_HDR_QOS_EN(pGTKRspPkt, 1);
		PlatformZeroMemory(&(Buffer[*pLength]), sQoSCtlLng);
		*pLength += sQoSCtlLng;
	}
#endif //0
	//-------------------------------------------------------------------------
	// Security Header: leave space for it if necessary.
	//-------------------------------------------------------------------------

#if 1
	switch (psecuritypriv->dot11PrivacyAlgrthm)
	{
		case _WEP40_:
		case _WEP104_:
			EncryptionHeadOverhead = 4;
			break;
		case _TKIP_:
			EncryptionHeadOverhead = 8;	
			break;			
		case _AES_:
			EncryptionHeadOverhead = 8;
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			EncryptionHeadOverhead = 18;
			break;
#endif //CONFIG_WAPI_SUPPORT
		default:
			EncryptionHeadOverhead = 0;
	}
	
	if(EncryptionHeadOverhead > 0)
	{
		_rtw_memset(&(pframe[*pLength]), 0,EncryptionHeadOverhead);
	       	*pLength += EncryptionHeadOverhead;
		//SET_80211_HDR_WEP(pGTKRspPkt, 1);  //Suggested by CCW.
		//GTK's privacy bit is done by FW
		//SetPrivacy(fctrl);
	}	
#endif //1
	//-------------------------------------------------------------------------
	// Frame Body.
	//-------------------------------------------------------------------------
	pGTKRspPkt =  (u8*)(pframe+ *pLength); 
	// LLC header
	_rtw_memcpy(pGTKRspPkt, LLCHeader, 8);	
	*pLength += 8;

	// GTK element
	pGTKRspPkt += 8;
	
	//GTK frame body after LLC, part 1
	_rtw_memcpy(pGTKRspPkt, GTKbody_a, 11);	
	*pLength += 11;
	pGTKRspPkt += 11;
	//GTK frame body after LLC, part 2
	_rtw_memset(&(pframe[*pLength]), 0, 88);
	*pLength += 88;
	pGTKRspPkt += 88;

}
#endif //CONFIG_GTK_OL

static void rtl8192e_set_FwRsvdPagePkt(PADAPTER padapter, BOOLEAN bDLFinished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;	
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u32	BeaconLength=0, ProbeRspLength=0, PSPollLength=0;
	u32	NullDataLength=0, QosNullLength=0, BTQosNullLength=0;
	u32	ProbeReqLength=0;
	u8	*ReservedPagePacket;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum=0, CurtPktPageNum=0, RsvdPageNum=0;
	u16	BufIndex, PageSize = PAGE_SIZE_TX_92E;
	u32	TotalPacketLen, MaxRsvdPageBufSize=0;
	RSVDPAGE_LOC	RsvdPageLoc;
#ifdef CONFIG_WOWLAN	
	u32	ARPLegnth = 0, GTKLegnth = 0, PNOLength = 0, ScanInfoLength = 0;
	u32	SSIDLegnth = 0;
	struct security_priv *psecuritypriv = &padapter->securitypriv; //added by xx
	u8 currentip[4];
	u8 cur_dot11txpn[8];
#ifdef CONFIG_GTK_OL
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info * psta;
	u8 kek[RTW_KEK_LEN];
	u8 kck[RTW_KCK_LEN];
#endif
#endif
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif // DBG_CONFIG_ERROR_DETECT

	//DBG_871X("%s---->\n", __FUNCTION__);

	pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	psrtpriv = &pHalData->srestpriv;
#endif
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);

	//RsvdPageNum = BCNQ_PAGE_NUM_8723B + WOWLAN_PAGE_NUM_8723B;
	RsvdPageNum = TOTAL_RSVD_PAGE_NUMBER_8192E;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	//3 (1) beacon
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	CurtPktPageNum = (u8)PageNum_256(TxDescLen + BeaconLength);
	//If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware
	if (CurtPktPageNum == 1)
	{
		CurtPktPageNum += 1;
	}
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//3 (2) ps-poll
	RsvdPageLoc.LocPsPoll = TotalPageNum;
	ConstructPSPoll(padapter, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, _TRUE, _FALSE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: PS-POLL %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (PSPollLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + PSPollLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//3 (3) null data
	RsvdPageLoc.LocNullData = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&NullDataLength,
		get_my_bssid(&pmlmeinfo->network),
		_FALSE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, _FALSE, _FALSE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: NULL DATA %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (NullDataLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + NullDataLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

#if 0
	//3 (4) probe response
	RsvdPageLoc.LocProbeRsp = TotalPageNum;
	ConstructProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		get_my_bssid(&pmlmeinfo->network),
		_FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, _FALSE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (ProbeRspLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + ProbeRspLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);
#endif

	//3 (5) Qos null data
	RsvdPageLoc.LocQosNull = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&QosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, _FALSE, _FALSE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (QosNullLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + QosNullLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//3 (6) BT Qos null data
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, _FALSE, _TRUE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: BT QOS NULL DATA %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (BTQosNullLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + BTQosNullLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

#ifdef CONFIG_WOWLAN
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
	//if (pwrctl->wowlan_mode == _TRUE) {
		//BufIndex += (CurtPktPageNum*PageSize);

	//3(7) ARP RSP
	rtw_get_current_ip_address(padapter, currentip);
	RsvdPageLoc.LocArpRsp= TotalPageNum;
#ifdef DBG_CONFIG_ERROR_DETECT
	if(psrtpriv->silent_reset_inprogress == _FALSE)
#endif //DBG_CONFIG_ERROR_DETECT
	{
	ConstructARPResponse(
		padapter, 
		&ReservedPagePacket[BufIndex],
		&ARPLegnth,
		currentip
		);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], ARPLegnth, _FALSE, _FALSE, _TRUE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: ARP RSP %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (ARPLegnth+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + ARPLegnth);
	}
#ifdef DBG_CONFIG_ERROR_DETECT
	else
		CurtPktPageNum = (u8)PageNum_256(256);
#endif //DBG_CONFIG_ERROR_DETECT
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//3(8) SEC IV
	rtw_get_sec_iv(padapter, cur_dot11txpn, get_my_bssid(&pmlmeinfo->network));
	RsvdPageLoc.LocRemoteCtrlInfo = TotalPageNum;
	_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen, cur_dot11txpn, _AES_IV_LEN_);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: SEC IV %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], _AES_IV_LEN_);

	CurtPktPageNum = (u8)PageNum_256(_AES_IV_LEN_);

	TotalPageNum += CurtPktPageNum;
	
#ifdef CONFIG_GTK_OL
	BufIndex += (CurtPktPageNum*PageSize);

	//if the ap staion info. exists, get the kek, kck from staion info.
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) 
	{
		_rtw_memset(kek, 0, RTW_KEK_LEN);
		_rtw_memset(kck, 0, RTW_KCK_LEN);
		DBG_8192C("%s, KEK, KCK download rsvd page all zero \n", __func__);
	}
	else
	{
		_rtw_memcpy(kek, psta->kek, RTW_KEK_LEN);
		_rtw_memcpy(kck, psta->kck, RTW_KCK_LEN);
	}
	
	//3(9) KEK, KCK
	RsvdPageLoc.LocGTKInfo = TotalPageNum;
	_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen, kck, RTW_KCK_LEN);
	_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen+RTW_KCK_LEN, kek, RTW_KEK_LEN);
	
#if 0
	{
		int i;
		printk("\ntoFW KCK: ");
		for(i=0;i<16; i++)
			printk(" %02x ", kck[i]);
		printk("\ntoFW KEK: ");
		for(i=0;i<16; i++)
			printk(" %02x ", kek[i]);
		printk("\n");
	}
#endif

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: KEK KCK %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//3(10) GTK Response
	RsvdPageLoc.LocGTKRsp= TotalPageNum;
	ConstructGTKResponse(
		padapter, 
		&ReservedPagePacket[BufIndex],
		&GTKLegnth
		);

	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], GTKLegnth, _FALSE, _FALSE, _TRUE);
#if 0
	{
		int gj;
		printk("123GTK pkt=> \n");
		for(gj=0; gj < GTKLegnth+TxDescLen; gj++) {
			printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
			if ((gj + 1)%16==0)
				printk("\n");
		}
		printk(" <=end\n");
	}
#endif

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: GTK RSP %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (TxDescLen + GTKLegnth));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + GTKLegnth);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//below page is empty for GTK extension memory
	//3(11) GTK EXT MEM
	RsvdPageLoc.LocGTKEXTMEM= TotalPageNum;

	CurtPktPageNum = 1;

	TotalPageNum += CurtPktPageNum;

	TotalPacketLen = BufIndex-TxDescLen + 256; //extension memory for FW
#else
	TotalPacketLen = BufIndex-TxDescLen + sizeof (union pn48); //IV len
#endif //CONFIG_GTK_OL
	} else
#endif //CONFIG_WOWLAN
	{
#ifdef CONFIG_PNO_SUPPORT
		if (pwrctl->pno_in_resume == _FALSE) {
			//Probe Request
			RsvdPageLoc.LocProbePacket = TotalPageNum;
			ConstructProbeReq(
				padapter,
				&ReservedPagePacket[BufIndex],
				&ProbeReqLength);

			rtl8192e_fill_fake_txdesc(padapter,
				&ReservedPagePacket[BufIndex-TxDescLen],
				ProbeReqLength, _FALSE, _FALSE, _FALSE);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("probe req pkt=> \n");
			for(gj=0; gj < ProbeReqLength + TxDescLen; gj++) {
				printk(" %02x ",ReservedPagePacket[BufIndex- TxDescLen + gj]);
				if ((gj + 1)%8==0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum =
				(u8)PageNum_256(TxDescLen + ProbeReqLength);

			TotalPageNum += CurtPktPageNum;

			BufIndex += (CurtPktPageNum*PageSize);

			//PNO INFO Page
			RsvdPageLoc.LocPNOInfo = TotalPageNum;
			ConstructPnoInfo(padapter, &ReservedPagePacket[BufIndex -TxDescLen], &PNOLength);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("PNO pkt=> \n");
			for(gj=0; gj < PNOLength; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen +gj]);
				if ((gj + 1)%8==0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif

			CurtPktPageNum = (u8)PageNum_256(PNOLength);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			//SSID List Page
			RsvdPageLoc.LocSSIDInfo = TotalPageNum;
			ConstructSSIDList(padapter, &ReservedPagePacket[BufIndex-TxDescLen], &SSIDLegnth);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("SSID list pkt=> \n");
			for(gj=0; gj < SSIDLegnth; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%8==0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum = (u8)PageNum_256(SSIDLegnth);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			//Scan Info Page
			RsvdPageLoc.LocScanInfo = TotalPageNum;
			ConstructScanInfo(padapter, &ReservedPagePacket[BufIndex-TxDescLen], &ScanInfoLength);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("Scan info pkt=> \n");
			for(gj=0; gj < ScanInfoLength; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%8==0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum = (u8)PageNum_256(ScanInfoLength);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			TotalPacketLen = BufIndex + ScanInfoLength;
		} else {
		TotalPacketLen = BufIndex + BTQosNullLength;
	}
#else //CONFIG_PNO_SUPPORT
		TotalPacketLen = BufIndex + BTQosNullLength;
#endif
	}

	if(TotalPacketLen > MaxRsvdPageBufSize)
	{
		DBG_871X("%s(): ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",__FUNCTION__,
			TotalPacketLen,MaxRsvdPageBufSize);
		goto error;
	}
	else
	{
		// update attribute
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
		dump_mgntframe(padapter, pcmdframe);
#else
		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif
	}

	DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n", __FUNCTION__,TotalPacketLen,TotalPageNum);
	if(check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtl8192e_set_FwRsvdPage_cmd(padapter, &RsvdPageLoc);
		rtl8192e_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);
	} else {
		rtl8192e_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);
#ifdef CONFIG_PNO_SUPPORT
		if(pwrctl->pno_in_resume)
			rtl8723b_set_FwScanOffloadInfo_cmd(padapter,
					&RsvdPageLoc, 0);
		else
			rtl8723b_set_FwScanOffloadInfo_cmd(padapter,
					&RsvdPageLoc, 1);
#endif
	}
	return;

error:

	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

static void rtl8192e_set_FwAOACGlobalInfo_Cmd(PADAPTER padapter,  u8 group_alg, u8 pairwise_alg)
{
	u8 u1H2CAOACGlobalInfoParm[H2C_AOAC_GLOBAL_INFO_LEN]={0};

	DBG_871X("%s(): group_alg=%d pairwise_alg=%d\n", __func__, group_alg, pairwise_alg);

	SET_H2CCMD_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(u1H2CAOACGlobalInfoParm, pairwise_alg);
	SET_H2CCMD_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(u1H2CAOACGlobalInfoParm, group_alg);
	
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CAOACGlobalInfoParm:", u1H2CAOACGlobalInfoParm, H2C_AOAC_GLOBAL_INFO_LEN);

	FillH2CCmd_8192E(padapter, H2C_AOAC_GLOBAL_INFO, H2C_AOAC_GLOBAL_INFO_LEN, u1H2CAOACGlobalInfoParm);
}

void rtl8192e_download_rsvd_page(PADAPTER padapter, u8 mstatus)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	BOOLEAN		bcn_valid = _FALSE;
	u8	DLBcnCount=0;
	u32 poll = 0;
	u8 val8;

_func_enter_;

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d mstatus(%x)\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), mstatus);

	if(mstatus == RT_MEDIA_CONNECT)
	{
		BOOLEAN bRecover = _FALSE;
		u8 v8;

		// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		// Suggested by filen. Added by tynli.
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));

		// set REG_CR bit 8
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 |= BIT(0); // ENSWBCN
		rtw_write8(padapter,  REG_CR+1, v8);

		// Disable Hw protection for a time which revserd for Hw sending beacon.
		// Fix download reserved page packet fail that access collision with the protection time.
		// 2010.05.11. Added by tynli.
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~BIT(3);
		val8 |= BIT(4);
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
		if (pHalData->RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		// To tell Hw the packet is not a real beacon frame.
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl & ~BIT(6));
		pHalData->RegFwHwTxQCtrl &= ~BIT(6);

		// Clear beacon valid check bit.
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do
		{
#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode)
				rtl8192e_set_AP_FwRsvdPagePkt(padapter, 0);
			else
				rtw_hal_set_fw_rsvd_page(padapter, 0);
#else
			// download rsvd page.
			rtw_hal_set_fw_rsvd_page(padapter, 0);
#endif
			DLBcnCount++;
			do
			{
				rtw_yield_os();
				//rtw_mdelay_os(10);
				// check rsvd page download OK.
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8*)(&bcn_valid));
				poll++;
			} while(!bcn_valid && (poll%10)!=0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
			
		}while(!bcn_valid && DLBcnCount<=100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

		if(padapter->bSurpriseRemoved || padapter->bDriverStopped)
		{
		}
		else if(!bcn_valid)
			DBG_871X(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter) ,DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			DBG_871X(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		}

		// 2010.05.11. Added by tynli.
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= BIT(3);
		val8 &= ~BIT(4);
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		// To make sure that if there exists an adapter which would like to send beacon.
		// If exists, the origianl value of 0x422[6] will be 1, we should check this to
		// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		// the beacon cannot be sent by HW.
		// 2010.06.23. Added by tynli.
		if(bRecover)
		{
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl | BIT(6));
			pHalData->RegFwHwTxQCtrl |= BIT(6);
		}

		// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 &= ~BIT(0); // ~ENSWBCN
		rtw_write8(padapter, REG_CR+1, v8);
	}

_func_exit_;
}

void rtl8192e_set_FwMediaStatusRpt_cmd(PADAPTER	padapter, u8 mstatus, u8 macid)
{
	u8 u1H2CMediaStatusRptParm[H2C_MEDIA_STATUS_RPT_LEN]={0};
	u8 macid_end = 0;

	DBG_871X("%s(): mstatus = %d macid=%d\n", __func__, mstatus, macid);

	SET_H2CCMD_MSRRPT_PARM_OPMODE(u1H2CMediaStatusRptParm, mstatus);
	SET_H2CCMD_MSRRPT_PARM_MACID_IND(u1H2CMediaStatusRptParm, 0);
	SET_H2CCMD_MSRRPT_PARM_MACID(u1H2CMediaStatusRptParm, macid);
	SET_H2CCMD_MSRRPT_PARM_MACID_END(u1H2CMediaStatusRptParm, macid_end);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CMediaStatusRptParm:", u1H2CMediaStatusRptParm, H2C_MEDIA_STATUS_RPT_LEN);
	FillH2CCmd_8192E(padapter, H2C_MEDIA_STATUS_RPT, H2C_MEDIA_STATUS_RPT_LEN, u1H2CMediaStatusRptParm);
}

static void rtl8192e_set_FwDisconDecision_cmd(PADAPTER padapter, u8 benable)
{
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN]={0};
	u8 adopt = 1, check_period = 10, trypkt_num = 0;

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, benable);
	SET_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	SET_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CDisconDecisionParm:", u1H2CDisconDecisionParm, H2C_DISCON_DECISION_LEN);

	FillH2CCmd_8192E(padapter, H2C_DISCON_DECISION, H2C_DISCON_DECISION_LEN, u1H2CDisconDecisionParm);
}

static void rtl8192e_set_FwKeepAlive_cmd(PADAPTER padapter, u8 benable, u8 pkt_type)
{
	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN]={0};
	u8 adopt = 1, check_period = 5;

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, benable);
	SET_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CKeepAliveParm:", u1H2CKeepAliveParm, H2C_KEEP_ALIVE_CTRL_LEN);

	FillH2CCmd_8192E(padapter, H2C_KEEP_ALIVE, H2C_KEEP_ALIVE_CTRL_LEN, u1H2CKeepAliveParm);
}

static void rtl8192e_set_FwWoWlanCtrl_Cmd(PADAPTER padapter, u8 bFuncEn)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	u8 u1H2CWoWlanCtrlParm[H2C_WOWLAN_LEN]={0};
	u8 discont_wake = 1, gpionum = 0, gpio_dur = 0, hw_unicast = 0, gpio_pulse_cnt=100;
	u8 sdio_wakeup_enable = 0;
	u8 gpio_high_active = 0; //0: low active, 1: high active
	u8 magic_pkt = 1;
	
#ifdef CONFIG_GPIO_WAKEUP
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif

#ifdef CONFIG_PNO_SUPPORT
	if (!ppwrpriv->wowlan_pno_enable) {
		magic_pkt = 1;
	}
#endif

	if (psecpriv->dot11PrivacyAlgrthm == _WEP40_ || psecpriv->dot11PrivacyAlgrthm == _WEP104_)
		hw_unicast = 1;

	DBG_871X("%s(): bFuncEn=%d\n", __func__, bFuncEn);

	SET_H2CCMD_WOWLAN_FUNC_ENABLE(u1H2CWoWlanCtrlParm, bFuncEn);
	SET_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(u1H2CWoWlanCtrlParm, magic_pkt);
	SET_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(u1H2CWoWlanCtrlParm, hw_unicast);
	SET_H2CCMD_WOWLAN_ALL_PKT_DROP(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_ACTIVE(u1H2CWoWlanCtrlParm, gpio_high_active);
#ifndef CONFIG_GTK_OL
	SET_H2CCMD_WOWLAN_REKEY_WAKE_UP(u1H2CWoWlanCtrlParm, 1); 
#endif //!CONFIG_GTK_OL
	SET_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(u1H2CWoWlanCtrlParm, discont_wake); 
	SET_H2CCMD_WOWLAN_GPIONUM(u1H2CWoWlanCtrlParm, gpionum);
	SET_H2CCMD_WOWLAN_DATAPIN_WAKE_UP(u1H2CWoWlanCtrlParm, sdio_wakeup_enable);
	SET_H2CCMD_WOWLAN_GPIO_DURATION(u1H2CWoWlanCtrlParm, gpio_dur);
	SET_H2CCMD_WOWLAN_GPIO_PULSE_EN(u1H2CWoWlanCtrlParm, 1);
	SET_H2CCMD_WOWLAN_GPIO_PULSE_COUNT(u1H2CWoWlanCtrlParm, 0x09);
	
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CWoWlanCtrlParm:", u1H2CWoWlanCtrlParm, H2C_WOWLAN_LEN);
	DBG_871X("u1H2CWoWlanCtrlParm:%08x", (u32) u1H2CWoWlanCtrlParm[0]);

	FillH2CCmd_8192E(padapter, H2C_WOWLAN, H2C_WOWLAN_LEN, u1H2CWoWlanCtrlParm);
}

static void rtl8192e_set_FwRemoteWakeCtrl_Cmd(PADAPTER padapter, u8 benable)
{
	u8 u1H2CRemoteWakeCtrlParm[H2C_REMOTE_WAKE_CTRL_LEN]={0};
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	u8 res = 0, count = 0;

	DBG_871X("%s(): Enable=%d\n", __func__, benable);

#ifdef CONFIG_PNO_SUPPORT
	SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(u1H2CRemoteWakeCtrlParm, benable);
	SET_H2CCMD_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, benable);
#endif

	if (!ppwrpriv->wowlan_pno_enable) {
	SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(u1H2CRemoteWakeCtrlParm, benable);
	SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 1);
#ifdef CONFIG_GTK_OL
	if(psecuritypriv->binstallKCK_KEK == _TRUE && psecuritypriv->dot11PrivacyAlgrthm == _AES_)
	{
		SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 1);
	}
	else
	{
		DBG_871X("no kck or security is not AES\n");
		SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 0);
	}
#endif //CONFIG_GTK_OL

	SET_H2CCMD_REMOTE_WAKE_CTRL_FW_UNICAST_EN(u1H2CRemoteWakeCtrlParm, 1);

		if ((psecuritypriv->dot11PrivacyAlgrthm == _AES_) || (psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_))
		{
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(u1H2CRemoteWakeCtrlParm, 0);
		}
		else
		{
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(u1H2CRemoteWakeCtrlParm, 1);
		}
	}
exit:
	DBG_871X("H2C  81[0]:%02x , 81[2]:%02x\n", u1H2CRemoteWakeCtrlParm[0], u1H2CRemoteWakeCtrlParm[2]);
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CRemoteWakeCtrlParm:", u1H2CRemoteWakeCtrlParm, H2C_REMOTE_WAKE_CTRL_LEN);
	FillH2CCmd_8192E(padapter, H2C_REMOTE_WAKE_CTRL,
		H2C_REMOTE_WAKE_CTRL_LEN, u1H2CRemoteWakeCtrlParm);
#ifdef CONFIG_PNO_SUPPORT
	if (ppwrpriv->wowlan_pno_enable && ppwrpriv->pno_in_resume == _FALSE) {
		res = rtw_read8(padapter, REG_PNO_STATUS);
		DBG_871X("cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", res);
		while(!(res&BIT(7)) && count < 25) {
			DBG_871X("[%d] cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", count, res);
			res = rtw_read8(padapter, REG_PNO_STATUS);
			count++;
			rtw_msleep_os(2);
		}
		DBG_871X("cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", res);
	}
#endif //CONFIG_PNO_SUPPORT
}

static void rtl8192e_set_FwWoWlanRelated_cmd(_adapter* padapter, u8 enable)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u8	pkt_type = 0;
	
	DBG_871X_LEVEL(_drv_always_, "+%s()+: enable=%d\n", __func__, enable);
_func_enter_;
	if(enable)
	{
		rtl8192e_set_FwAOACGlobalInfo_Cmd(padapter, psecpriv->dot118021XGrpPrivacy, psecpriv->dot11PrivacyAlgrthm);

		rtl8192e_download_rsvd_page(padapter, RT_MEDIA_CONNECT);	//RT_MEDIA_CONNECT will confuse in the future

		if(!(ppwrpriv->wowlan_pno_enable))
		{
			psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(pmlmepriv));
			if (psta != NULL)
				rtl8192e_set_FwMediaStatusRpt_cmd(padapter, RT_MEDIA_CONNECT, psta->mac_id);
		}	
		else
			DBG_871X("%s(): Disconnected, no FwMediaStatusRpt CONNECT\n",__FUNCTION__);

		rtw_msleep_os(2);

		if(!(ppwrpriv->wowlan_pno_enable)) {
			rtl8192e_set_FwDisconDecision_cmd(padapter, enable);
			rtw_msleep_os(2);

			if ((psecpriv->dot11PrivacyAlgrthm != _WEP40_) || (psecpriv->dot11PrivacyAlgrthm != _WEP104_))
				pkt_type = 1;
			rtl8192e_set_FwKeepAlive_cmd(padapter, enable, pkt_type);
			rtw_msleep_os(2);
		}

		rtl8192e_set_FwRemoteWakeCtrl_Cmd(padapter, enable);
		rtw_msleep_os(2);

		rtl8192e_set_FwWoWlanCtrl_Cmd(padapter, enable);
	}
	else
	{
#if 0
		dump_TX_FIFO(padapter);
#endif
		rtl8192e_set_FwRemoteWakeCtrl_Cmd(padapter, enable);
		rtw_msleep_os(2);
		rtl8192e_set_FwWoWlanCtrl_Cmd(padapter, enable);
	}
	
_func_exit_;
	DBG_871X_LEVEL(_drv_always_, "-%s()-\n", __func__);
	return ;
}

void rtl8192e_set_wowlan_cmd(_adapter* padapter, u8 enable)
{
	rtl8192e_set_FwWoWlanRelated_cmd(padapter, enable);
}
#endif  //CONFIG_WOWLAN

#ifdef CONFIG_AP_WOWLAN
static void rtl8192e_set_FwAPWoWlanCtrl_Cmd(_adapter* padapter, u8 bFuncEn)
{
	u8 u1H2CAPWoWlanCtrlParm[H2C_AP_WOW_GPIO_CTRL_LEN]={0};
	u8 discont_wake = 1, gpionum = 0, gpio_dur = 0;
	u8 gpio_high_active = 1; //0: low active, 1: high active
	u8 gpio_pulse = bFuncEn;
#ifdef CONFIG_GPIO_WAKEUP
	gpionum = WAKEUP_GPIO_IDX;
#endif

	DBG_871X("%s(): bFuncEn=%d\n", __func__, bFuncEn);

	if (bFuncEn)
		gpio_dur = 16;
	else
		gpio_dur = 0;

#if 0
	SET_H2CCMD_AP_WOW_GPIO_CTRL_INDEX(u1H2CAPWoWlanCtrlParm,
			gpionum);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_PLUS(u1H2CAPWoWlanCtrlParm,
			gpio_pulse);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_HIGH_ACTIVE(u1H2CAPWoWlanCtrlParm,
			gpio_high_active);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_EN(u1H2CAPWoWlanCtrlParm,
			bFuncEn);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_DURATION(u1H2CAPWoWlanCtrlParm,
			gpio_dur);
#else
	u1H2CAPWoWlanCtrlParm[0] = 0xee;
	u1H2CAPWoWlanCtrlParm[1] = 0x87;
	u1H2CAPWoWlanCtrlParm[2] = 0x0;
	u1H2CAPWoWlanCtrlParm[3] = 0x01;
#endif

	FillH2CCmd_8192E(padapter, H2C_8192E_AP_WOW_GPIO_CTRL,
			H2C_AP_WOW_GPIO_CTRL_LEN, u1H2CAPWoWlanCtrlParm);
}

static void rtl8192e_set_Fw_AP_Offload_Cmd(_adapter* padapter, u8 bFuncEn)
{
	u8 u1H2CAPOffloadCtrlParm[H2C_WOWLAN_LEN]={0};

	DBG_871X("%s(): bFuncEn=%d\n", __func__, bFuncEn);

	SET_H2CCMD_AP_WOWLAN_EN(u1H2CAPOffloadCtrlParm, bFuncEn);

	FillH2CCmd_8192E(padapter, H2C_8192E_AP_OFFLOAD,
			H2C_AP_OFFLOAD_LEN, u1H2CAPOffloadCtrlParm);
}

static void rtl8192e_set_AP_PS_Cmd(_adapter* padapter, u8 bFuncEn)
{
	u8 ap_ps_parm[H2C_AP_PS_LEN]={0};
	DBG_871X("%s(): bFuncEn=%d\n", __func__, bFuncEn);

	SET_H2CCMD_AP_WOW_PS_EN(ap_ps_parm, bFuncEn);
	SET_H2CCMD_AP_WOW_PS_32K_EN(ap_ps_parm, bFuncEn);
	SET_H2CCMD_AP_WOW_PS_RF(ap_ps_parm, bFuncEn);

	if (bFuncEn) {
		SET_H2CCMD_AP_WOW_PS_DURATION(ap_ps_parm, 0x32);
	} else {
		SET_H2CCMD_AP_WOW_PS_DURATION(ap_ps_parm, 0x0);
	}

	FillH2CCmd_8192E(padapter, H2C_8192E_SAP_PS,
			H2C_AP_PS_LEN, ap_ps_parm);
}

static void rtl8192e_set_AP_FwWoWlan_cmd(_adapter* padapter, u8 enable)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u8	pkt_type = 0;

	DBG_871X_LEVEL(_drv_always_, "+%s()+: enable=%d\n", __func__, enable);
_func_enter_;
	if (enable) {
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(padapter) == _TRUE &&
			check_buddy_fwstate(padapter, WIFI_AP_STATE) == _TRUE) {
				rtl8192e_set_FwJoinBssReport_cmd(padapter->pbuddy_adapter, RT_MEDIA_CONNECT);
				issue_beacon(padapter->pbuddy_adapter, 0);
		} else {
			rtl8192e_set_FwJoinBssReport_cmd(padapter, RT_MEDIA_CONNECT);
			issue_beacon(padapter, 0);
		}
#else
		rtl8192e_set_FwJoinBssReport_cmd(padapter, RT_MEDIA_CONNECT);
		issue_beacon(padapter, 0);
#endif
	}
#if 0
	else

		dump_TX_FIFO(padapter);
#endif
	rtl8192e_set_FwAPWoWlanCtrl_Cmd(padapter, enable);
	rtw_msleep_os(10);
	rtl8192e_set_Fw_AP_Offload_Cmd(padapter, enable);
	rtw_msleep_os(10);

_func_exit_;
	DBG_871X_LEVEL(_drv_always_, "-%s()-\n", __func__);
	return ;
}

void rtl8192e_set_ap_wowlan_cmd(_adapter* padapter, u8 enable)
{
	rtl8192e_set_AP_FwWoWlan_cmd(padapter, enable);
}

void rtl8192e_set_ap_ps_wowlan_cmd(_adapter* padapter, u8 enable)
{
	rtl8192e_set_AP_PS_Cmd(padapter, enable);
}
#endif //CONFIG_AP_WOWLAN

#if 0
u1Byte
HwRateToMRate(
	IN 	u1Byte		rate
	)
{
	u1Byte	ret_rate = MGN_1M;

	switch(rate)
	{
	
		case DESC_RATE1M:		    ret_rate = MGN_1M;		break;
		case DESC_RATE2M:		    ret_rate = MGN_2M;		break;
		case DESC_RATE5_5M:	        ret_rate = MGN_5_5M;	break;
		case DESC_RATE11M:		    ret_rate = MGN_11M;		break;
		case DESC_RATE6M:		    ret_rate = MGN_6M;		break;
		case DESC_RATE9M:		    ret_rate = MGN_9M;		break;
		case DESC_RATE12M:		    ret_rate = MGN_12M;		break;
		case DESC_RATE18M:		    ret_rate = MGN_18M;		break;
		case DESC_RATE24M:		    ret_rate = MGN_24M;		break;
		case DESC_RATE36M:		    ret_rate = MGN_36M;		break;
		case DESC_RATE48M:		    ret_rate = MGN_48M;		break;
		case DESC_RATE54M:		    ret_rate = MGN_54M;		break;			
		case DESC_RATEMCS0:	        ret_rate = MGN_MCS0;	break;
		case DESC_RATEMCS1:	        ret_rate = MGN_MCS1;	break;
		case DESC_RATEMCS2:	        ret_rate = MGN_MCS2;	break;
		case DESC_RATEMCS3:	        ret_rate = MGN_MCS3;	break;
		case DESC_RATEMCS4:	        ret_rate = MGN_MCS4;	break;
		case DESC_RATEMCS5:	        ret_rate = MGN_MCS5;	break;
		case DESC_RATEMCS6:	        ret_rate = MGN_MCS6;	break;
		case DESC_RATEMCS7:	        ret_rate = MGN_MCS7;	break;
		case DESC_RATEMCS8:	        ret_rate = MGN_MCS8;	break;
		case DESC_RATEMCS9:	        ret_rate = MGN_MCS9;	break;
		case DESC_RATEMCS10:	    ret_rate = MGN_MCS10;	break;
		case DESC_RATEMCS11:	    ret_rate = MGN_MCS11;	break;
		case DESC_RATEMCS12:	    ret_rate = MGN_MCS12;	break;
		case DESC_RATEMCS13:	    ret_rate = MGN_MCS13;	break;
		case DESC_RATEMCS14:	    ret_rate = MGN_MCS14;	break;
		case DESC_RATEMCS15:	    ret_rate = MGN_MCS15;	break;
		case DESC_RATEMCS16:	    ret_rate = MGN_MCS16;	break;
		case DESC_RATEMCS17:	    ret_rate = MGN_MCS17;	break;
		case DESC_RATEMCS18:	    ret_rate = MGN_MCS18;	break;
		case DESC_RATEMCS19:	    ret_rate = MGN_MCS19;	break;
		case DESC_RATEMCS20:	    ret_rate = MGN_MCS20;	break;
		case DESC_RATEMCS21:	    ret_rate = MGN_MCS21;	break;
		case DESC_RATEMCS22:	    ret_rate = MGN_MCS22;	break;
		case DESC_RATEMCS23:	    ret_rate = MGN_MCS23;	break;
		case DESC_RATEMCS24:	    ret_rate = MGN_MCS24;	break;
		case DESC_RATEMCS25:	    ret_rate = MGN_MCS25;	break;
		case DESC_RATEMCS26:	    ret_rate = MGN_MCS26;	break;
		case DESC_RATEMCS27:	    ret_rate = MGN_MCS27;	break;
		case DESC_RATEMCS28:	    ret_rate = MGN_MCS28;	break;
		case DESC_RATEMCS29:	    ret_rate = MGN_MCS29;	break;
		case DESC_RATEMCS30:	    ret_rate = MGN_MCS30;	break;
		case DESC_RATEMCS31:	    ret_rate = MGN_MCS31;	break;
		case DESC_RATEVHTSS1MCS0:	ret_rate = MGN_VHT1SS_MCS0;		break;
		case DESC_RATEVHTSS1MCS1:	ret_rate = MGN_VHT1SS_MCS1;		break;
		case DESC_RATEVHTSS1MCS2:	ret_rate = MGN_VHT1SS_MCS2;		break;
		case DESC_RATEVHTSS1MCS3:	ret_rate = MGN_VHT1SS_MCS3;		break;
		case DESC_RATEVHTSS1MCS4:	ret_rate = MGN_VHT1SS_MCS4;		break;
		case DESC_RATEVHTSS1MCS5:	ret_rate = MGN_VHT1SS_MCS5;		break;
		case DESC_RATEVHTSS1MCS6:	ret_rate = MGN_VHT1SS_MCS6;		break;
		case DESC_RATEVHTSS1MCS7:	ret_rate = MGN_VHT1SS_MCS7;		break;
		case DESC_RATEVHTSS1MCS8:	ret_rate = MGN_VHT1SS_MCS8;		break;
		case DESC_RATEVHTSS1MCS9:	ret_rate = MGN_VHT1SS_MCS9;		break;
		case DESC_RATEVHTSS2MCS0:	ret_rate = MGN_VHT2SS_MCS0;		break;
		case DESC_RATEVHTSS2MCS1:	ret_rate = MGN_VHT2SS_MCS1;		break;
		case DESC_RATEVHTSS2MCS2:	ret_rate = MGN_VHT2SS_MCS2;		break;
		case DESC_RATEVHTSS2MCS3:	ret_rate = MGN_VHT2SS_MCS3;		break;
		case DESC_RATEVHTSS2MCS4:	ret_rate = MGN_VHT2SS_MCS4;		break;
		case DESC_RATEVHTSS2MCS5:	ret_rate = MGN_VHT2SS_MCS5;		break;
		case DESC_RATEVHTSS2MCS6:	ret_rate = MGN_VHT2SS_MCS6;		break;
		case DESC_RATEVHTSS2MCS7:	ret_rate = MGN_VHT2SS_MCS7;		break;
		case DESC_RATEVHTSS2MCS8:	ret_rate = MGN_VHT2SS_MCS8;		break;
		case DESC_RATEVHTSS2MCS9:	ret_rate = MGN_VHT2SS_MCS9;		break;				
		case DESC_RATEVHTSS3MCS0:	ret_rate = MGN_VHT3SS_MCS0;		break;
		case DESC_RATEVHTSS3MCS1:	ret_rate = MGN_VHT3SS_MCS1;		break;
		case DESC_RATEVHTSS3MCS2:	ret_rate = MGN_VHT3SS_MCS2;		break;
		case DESC_RATEVHTSS3MCS3:	ret_rate = MGN_VHT3SS_MCS3;		break;
		case DESC_RATEVHTSS3MCS4:	ret_rate = MGN_VHT3SS_MCS4;		break;
		case DESC_RATEVHTSS3MCS5:	ret_rate = MGN_VHT3SS_MCS5;		break;
		case DESC_RATEVHTSS3MCS6:	ret_rate = MGN_VHT3SS_MCS6;		break;
		case DESC_RATEVHTSS3MCS7:	ret_rate = MGN_VHT3SS_MCS7;		break;
		case DESC_RATEVHTSS3MCS8:	ret_rate = MGN_VHT3SS_MCS8;		break;
		case DESC_RATEVHTSS3MCS9:	ret_rate = MGN_VHT3SS_MCS9;		break;				
		case DESC_RATEVHTSS4MCS0:	ret_rate = MGN_VHT4SS_MCS0;		break;
		case DESC_RATEVHTSS4MCS1:	ret_rate = MGN_VHT4SS_MCS1;		break;
		case DESC_RATEVHTSS4MCS2:	ret_rate = MGN_VHT4SS_MCS2;		break;
		case DESC_RATEVHTSS4MCS3:	ret_rate = MGN_VHT4SS_MCS3;		break;
		case DESC_RATEVHTSS4MCS4:	ret_rate = MGN_VHT4SS_MCS4;		break;
		case DESC_RATEVHTSS4MCS5:	ret_rate = MGN_VHT4SS_MCS5;		break;
		case DESC_RATEVHTSS4MCS6:	ret_rate = MGN_VHT4SS_MCS6;		break;
		case DESC_RATEVHTSS4MCS7:	ret_rate = MGN_VHT4SS_MCS7;		break;
		case DESC_RATEVHTSS4MCS8:	ret_rate = MGN_VHT4SS_MCS8;		break;
		case DESC_RATEVHTSS4MCS9:	ret_rate = MGN_VHT4SS_MCS9;		break;				
		
		default:							
			RT_TRACE(COMP_RECV, DBG_LOUD, ("HwRateToMRate(): Non supported Rate [%x]!!!\n",rate ));
			break;
	}	
	return ret_rate;
}

#endif

VOID
C2HRaReportHandler_8192E(
	IN	PADAPTER	Adapter,
	IN	u8*			CmdBuf,
	IN	u8			CmdLen
)
{
	u8 	Rate = CmdBuf[0] & 0x3F;
	u8	MacId = CmdBuf[1];
	BOOLEAN	bLDPC = CmdBuf[2] & BIT0;
	BOOLEAN	bTxBF = (CmdBuf[2] & BIT1) >> 1;
	BOOLEAN Collision_State = CmdBuf[3] & BIT0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	//ODM_DynamicARFBSelect(&pHalData->odmpriv, Rate, Collision_State);

	// pHalData->CurrentRARate = HwRateToMRate(Rate);
	ODM_UpdateInitRate(&pHalData->odmpriv, Rate);//For advance feature:ODM_TxPwrTrackSetPwr92E
}

void dump_txrpt_ccx_92e(IN	u8 *CmdBuf)
{
	u8 MacID,Unicast,LifeTimeOver,RetryOver,DataRetryCount,QueueTimeUs,FinalDataRateIndex;
		
	DBG_871X("============= %s ===========\n",__FUNCTION__);
	switch(GET_8192E_C2H_TX_RPT_QUEUE_SELECT(CmdBuf))
	{
		// AC Queue -------------------
		case 0x01: case 0x02:
			DBG_871X("QSEL: BK_QUEUE ");
			break;
		case 0x00: case 0x03:
			DBG_871X("QSEL: BE_QUEUE ");
			break;
		case 0x04: case 0x05:
			DBG_871X("QSEL:VI_QUEUE ");
			break;
		case 0x06: case 0x07:
			DBG_871X("QSEL:VO_QUEUE ");
			break;
		// ---------------------------
		case QSLT_BEACON:
			DBG_871X("QSEL:BEACON_QUEUE ");
			break;			
		case QSLT_HIGH:
			DBG_871X("QSEL: HIGH_QUEUE ");
			break;			
		case QSLT_MGNT:
			DBG_871X("QSEL:MGNT_QUEUE ");
			break;			
		case QSLT_CMD:
			DBG_871X("QSEL:TXCMD_QUEUE ");
			break;
			
		default:
			DBG_871X("QSEL:Invalid Queue Select ID !");
	}

	MacID = GET_8192E_C2H_TX_RPT_MAC_ID(CmdBuf);
	Unicast = GET_8192E_C2H_TX_RPT_PKT_BROCAST(CmdBuf);
	LifeTimeOver = GET_8192E_C2H_TX_RPT_LIFE_TIME_OVER(CmdBuf);
	RetryOver = GET_8192E_C2H_TX_RPT_RETRY_OVER(CmdBuf);
	DataRetryCount  = GET_8192E_C2H_TX_RPT_DATA_RETRY_CNT(CmdBuf);
	QueueTimeUs = GET_8192E_C2H_TX_RPT_QUEUE_TIME(CmdBuf) * USEC_UNIT_FOR_8192E_C2H_TX_RPT_QUEUE_TIME;
	FinalDataRateIndex = GET_8192E_C2H_TX_RPT_FINAL_DATA_RATE(CmdBuf);

	DBG_871X("MacID:%u,Unicast:%u,LifeTimeOver:%u,RetryOver:%u,DataRetryCount:%u,QueueTimeUs:%u,FinalDataRateIndex:%u"
			,MacID,Unicast,LifeTimeOver,RetryOver,DataRetryCount,QueueTimeUs,FinalDataRateIndex);

	
}

static VOID
C2HTxFeedbackHandler_8192E(
	IN	PADAPTER	Adapter,
	IN	u8			*CmdBuf,
	IN	u8			CmdLen
)
{
#ifdef CONFIG_XMIT_ACK
	if (GET_8192E_C2H_TX_RPT_RETRY_OVER(CmdBuf) | GET_8192E_C2H_TX_RPT_LIFE_TIME_OVER(CmdBuf)) {
		rtw_ack_tx_done(&Adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	} else {
		rtw_ack_tx_done(&Adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
	}
#endif
#ifdef DBG_CCX
	dump_txrpt_ccx_92e(CmdBuf);
#endif
	
}

VOID
C2HContentParsing8192E(
	IN	PADAPTER		Adapter,
	IN	u1Byte			c2hCmdId, 
	IN	u1Byte			c2hCmdLen,
	IN	pu1Byte 			tmpBuf
)
{

	switch(c2hCmdId)
	{
	case C2H_8192E_DBG:
		//DBG_871X("[C2H], C2H_8192E_DBG!!\n");
		break;

	case C2H_8192E_TXBF:
		//DBG_871X("[C2H], C2H_8192E_TXBF!!\n");
		//C2HTxBeamformingHandler_8192E(Adapter, tmpBuf, c2hCmdLen);
		break;	

	case C2H_8192E_TX_REPORT:
		//DBG_871X("[C2H], C2H_8192E_TX_REPORT!!\n");
		C2HTxFeedbackHandler_8192E(Adapter, tmpBuf, c2hCmdLen);
		break;
#ifdef CONFIG_BT_COEXIST
	case C2H_8192E_BT_INFO:
		//DBG_871X("[C2H], C2H_8192E_BT_INFO!!\n");
		//BT_BtInfoNotify(Adapter, tmpBuf, c2hCmdLen);
		rtw_btcoex_BtInfoNotify(Adapter, c2hCmdLen, tmpBuf);
		break;
#endif

	case C2H_8192E_BT_MP:
		//DBG_871X("[C2H], C2H_8192E_BT_MP!!\n");
#if(MP_DRIVER == 1)
		//MPTBT_FwC2hBtMpCtrl(Adapter, tmpBuf, c2hCmdLen);
#else
		//NDBG_FwC2hBtControl(Adapter, tmpBuf, c2hCmdLen);
#endif
		break;

	case C2H_8192E_RA_RPT:
		C2HRaReportHandler_8192E(Adapter, tmpBuf, c2hCmdLen); //for tx power tracking
		break;

#ifdef CONFIG_FW_C2H_DEBUG
	case C2H_8192E_FW_DEBUG:
		DBG_871X("[C2H], 8192E_FW_DEBUG.\n");
		Debug_FwC2H(Adapter, tmpBuf, c2hCmdLen);
		break;
#endif /* CONFIG_FW_C2H_DEBUG*/

	
	default:
		break;
	}

}


static inline void dump_c2h_packet(u8 *ptr , u8 length)
{
       int i;
       u8 tmpBuf[8];

	for(i=0; i<length;i++)
	{
	        if(i%8==0)
	        {
	               DBG_871X("############################# \n");
                       DBG_871X("");
	        }
               printk("%02X:",*(ptr+i));
               
       }
       printk("\n");
       DBG_871X("############################# \n");

}

VOID
C2HPacketHandler_8192E(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			Buffer,
	IN	u1Byte			Length
	)
{
        struct  c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)Buffer;
	u8	c2hCmdId=0, c2hCmdSeq=0, c2hCmdLen=0;
	u8* 	tmpBuf=NULL;
	u8	u1tmp=0, index=0;

	//PRINT_DATA(("C2HPacketHandler_8812"), Buffer, Length);
	c2hCmdId = Buffer[0];
	c2hCmdSeq = Buffer[1];
	c2hCmdLen = Length -2;
	tmpBuf = Buffer+2;
	
	//DBG_871X("[C2H packet], c2hCmdId=0x%x, c2hCmdSeq=0x%x, c2hCmdLen=%d\n", 
	//	c2hCmdId, c2hCmdSeq, c2hCmdLen);

        //dump_c2h_packet(tmpBuf,c2hCmdLen);
       
#ifdef CONFIG_BT_COEXIST
	if (Length>16) {
		DBG_871X("[C2H packet], c2hCmdId=0x%x, c2hCmdSeq=0x%x, c2hCmdLen=%d\n", c2hCmdId, c2hCmdSeq, c2hCmdLen);
		rtw_warn_on(1);
	}
	
	if (c2hCmdId == C2H_8192E_BT_INFO) {
		/* enqueue */
		if ((c2h_evt = (struct c2h_evt_hdr_88xx *)rtw_zmalloc(16)) != NULL) {
			_rtw_memcpy(c2h_evt, Buffer, Length);
			c2h_evt->plen = Length - 2;
			//DBG_871X("-[C2H packet], id=0x%x, seq=0x%x, plen=%d\n", c2h_evt->id, c2h_evt->seq, c2h_evt->plen);
			rtw_c2h_wk_cmd(Adapter, (u8 *)c2h_evt);
		}
	}
	else
#endif /* CONFIG_BT_COEXIST */
	{	
	        C2HContentParsing8192E(Adapter, c2hCmdId, c2hCmdLen, tmpBuf);
	        //PlatformEFIOWrite1Byte(Adapter, REG_C2HEVT_CLEAR_8812, C2H_EVT_HOST_CLOSE);
        }
}


s32 c2h_handler_8192e(PADAPTER padapter, u8 *buf)
{
	struct c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = _SUCCESS;

	if (c2h_evt == NULL) {
		DBG_8192C("%s c2h_evt is NULL\n",__FUNCTION__);
		ret = _FAIL;
		goto exit;
	}

	C2HContentParsing8192E(padapter, c2h_evt->id, c2h_evt->plen, c2h_evt->payload);

exit:
	return ret;
}

#ifdef CONFIG_BT_COEXIST

void ConstructBtNullFunctionData(
	PADAPTER padapter,
	u8		*pframe,
	u32		*pLength,
	u8		*StaAddr,
	u8		bQoS,
	u8		AC,
	u8		bEosp,
	u8		bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bssid[ETH_ALEN];

	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	if (NULL == StaAddr)
	{
		_rtw_memcpy(bssid, adapter_mac_addr(padapter), ETH_ALEN);
		StaAddr = bssid;
	}
	
	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave)
	{
		SetPwrMgt(fctrl);
	}
	
	SetFrDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetDuration(pwlanhdr, 0);
	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE) {
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos*)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

static void SetFwRsvdPagePkt_BTCoex(PADAPTER padapter)
{

	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;	
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u32	BeaconLength=0;
	u32	BTQosNullLength=0;
	u8	*ReservedPagePacket;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum=0, CurtPktPageNum=0, RsvdPageNum=0;
	u16	BufIndex, PageSize = 256;
	u32	TotalPacketLen, MaxRsvdPageBufSize=0;
	RSVDPAGE_LOC	RsvdPageLoc;

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	//RsvdPageNum = BCNQ_PAGE_NUM_8723B + WOWLAN_PAGE_NUM_8723B;
	RsvdPageNum = RSVD_PAGE_NUM_8192E;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	//3 (1) beacon
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	CurtPktPageNum = (u8)PageNum_256(TxDescLen + BeaconLength);
	//If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware
	if (CurtPktPageNum == 1)
	{
		CurtPktPageNum += 1;
	}
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);
	
	// Jump to lastest page
	if (BufIndex < (MaxRsvdPageBufSize - PageSize))
	{
		BufIndex = TxDescOffset + (MaxRsvdPageBufSize - PageSize);
		TotalPageNum = RSVD_PAGE_NUM_8192E-1;
	}
	
	//3 (6) BT Qos null data
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	ConstructBtNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		NULL,
		_TRUE, 0, 0, _FALSE);
	rtl8192e_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, _FALSE, _TRUE, _FALSE);

	//DBG_871X("%s(): HW_VAR_SET_TX_CMD: BT QOS NULL DATA %p %d\n", 
	//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (BTQosNullLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_256(TxDescLen + BTQosNullLength);

	TotalPageNum += CurtPktPageNum;


	TotalPacketLen = BufIndex + BTQosNullLength;
	if(TotalPacketLen > MaxRsvdPageBufSize)
	{
		DBG_871X("%s(): ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",__FUNCTION__,
			TotalPacketLen,MaxRsvdPageBufSize);
		goto error;
	}

	// update attribute
	pattrib = &pcmdframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
	dump_mgntframe(padapter, pcmdframe);
#else
	dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif
	

	//DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n", __FUNCTION__,TotalPacketLen,TotalPageNum);

	rtl8192e_set_FwRsvdPage_cmd(padapter, &RsvdPageLoc);
	rtl8192e_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);

	return;

error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

void rtl8192e_download_BTCoex_AP_mode_rsvd_page(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN bRecover = _FALSE;
	BOOLEAN	bcn_valid = _FALSE;
	u8	DLBcnCount=0;
	u32 poll = 0;
	u8 val8;


	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d fw_state=0x%08X\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), get_fwstate(&padapter->mlmepriv));

#ifdef CONFIG_DEBUG
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _FALSE)
	{
		DBG_8192C(FUNC_ADPT_FMT ": [WARNING] not in AP mode!!\n",
			FUNC_ADPT_ARG(padapter));
	}
#endif // CONFIG_DEBUG

	// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
	// Suggested by filen. Added by tynli.
	rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));

	// set REG_CR bit 8
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 |= BIT(0); // ENSWBCN
	rtw_write8(padapter,  REG_CR+1, val8);

	// Disable Hw protection for a time which revserd for Hw sending beacon.
	// Fix download reserved page packet fail that access collision with the protection time.
	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 &= ~BIT(3);
	val8 |= BIT(4);
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
	if (pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover = _TRUE;

	// To tell Hw the packet is not a real beacon frame.
	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	

	// Clear beacon valid check bit.
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

	DLBcnCount = 0;
	poll = 0;
	do
	{
		SetFwRsvdPagePkt_BTCoex(padapter);
		DLBcnCount++;
		do
		{
			rtw_yield_os();
			//rtw_mdelay_os(10);
			// check rsvd page download OK.
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8*)(&bcn_valid));
			poll++;
		} while(!bcn_valid && (poll%10)!=0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
		
	}while(!bcn_valid && DLBcnCount<=100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

	if(_TRUE == bcn_valid)
	{
		struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
		pwrctl->fw_psmode_iface_id = padapter->iface_id;
		DBG_871X(ADPT_FMT": DL RSVD page success! DLBcnCount:%u, poll:%u\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
	}
	else
	{
		DBG_871X(ADPT_FMT": DL RSVD page fail! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
		DBG_871X(ADPT_FMT": DL RSVD page fail! bSurpriseRemoved=%d\n",
			ADPT_ARG(padapter), padapter->bSurpriseRemoved);
		DBG_871X(ADPT_FMT": DL RSVD page fail! bDriverStopped=%d\n",
			ADPT_ARG(padapter), padapter->bDriverStopped);
	}
	
	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 |= BIT(3);
	val8 &= ~BIT(4);
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	// To make sure that if there exists an adapter which would like to send beacon.
	// If exists, the origianl value of 0x422[6] will be 1, we should check this to
	// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
	// the beacon cannot be sent by HW.
	// 2010.06.23. Added by tynli.
	if(bRecover)
	{
		pHalData->RegFwHwTxQCtrl |= BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	}

	// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 &= ~BIT(0); // ~ENSWBCN
	rtw_write8(padapter, REG_CR+1, val8);
}

#endif // CONFIG_BT_COEXIST
