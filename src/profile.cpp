// simplewall
// Copyright (c) 2016-2019 Henry++

#include "global.hpp"

size_t _app_addapplication (HWND hwnd, rstring path, time_t timestamp, time_t timer, time_t last_notify, bool is_silent, bool is_enabled, bool is_fromdb)
{
	if (path.IsEmpty () || path.At (0) == 0 || PathIsDirectory (path))
		return 0;

	// if file is shortcut - get location
	if (!is_fromdb)
	{
		if (_wcsnicmp (PathFindExtension (path), L".lnk", 4) == 0)
		{
			path = _app_getshortcutpath (app.GetHWND (), path);

			if (path.IsEmpty ())
				return 0;
		}
	}

	_app_resolvefilename (path);

	const size_t app_hash = path.Hash ();

	if (apps.find (app_hash) != apps.end ())
		return app_hash; // already exists

	ITEM_APP * ptr_app = &apps[app_hash]; // application pointer

	const bool is_ntoskrnl = (app_hash == config.ntoskrnl_hash);
	const time_t current_time = _r_unixtime_now ();

	rstring real_path;

	if (_wcsnicmp (path, L"\\device\\", 8) == 0) // device path
	{
		real_path = path;

		ptr_app->type = DataAppDevice;
	}
	else if (_wcsnicmp (path, L"S-1-", 4) == 0) // windows store (win8+)
	{
		ptr_app->type = DataAppUWP;

		_app_item_get (DataAppUWP, app_hash, nullptr, &real_path, &ptr_app->pdata, nullptr);
	}
	else if (PathIsNetworkPath (path)) // network path
	{
		real_path = path;

		ptr_app->type = DataAppNetwork;
	}
	else
	{
		real_path = path;

		if (!is_ntoskrnl && real_path.Find (OBJ_NAME_PATH_SEPARATOR) == rstring::npos)
		{
			if (_app_item_get (DataAppService, app_hash, nullptr, &real_path, &ptr_app->pdata, nullptr))
				ptr_app->type = DataAppService;

			else
				ptr_app->type = DataAppPico;
		}
		else
		{
			ptr_app->type = DataAppRegular;

			if (is_ntoskrnl) // "system" process
			{
				path.At (0) = _r_str_upper (path.At (0)); // fix "System" lowercase

				real_path = _r_path_expand (PATH_NTOSKRNL);
			}
		}
	}

	if (!real_path.IsEmpty ())
	{
		const DWORD dwAttr = GetFileAttributes (real_path);

		if (ptr_app->type == DataAppRegular)
		{
			ptr_app->is_temp = ((dwAttr != INVALID_FILE_ATTRIBUTES && (dwAttr & FILE_ATTRIBUTE_TEMPORARY) != 0)) || (_wcsnicmp (real_path, config.tmp1_dir, config.tmp1_length) == 0);
			ptr_app->is_system = !ptr_app->is_temp && (is_ntoskrnl || ((dwAttr != INVALID_FILE_ATTRIBUTES && (dwAttr & FILE_ATTRIBUTE_SYSTEM) != 0)) || (_wcsnicmp (real_path, config.windows_dir, config.wd_length) == 0));
		}

		ptr_app->is_signed = _app_getsignatureinfo (app_hash, real_path, &ptr_app->signer);
	}

	_app_applycasestyle (real_path.GetBuffer (), real_path.GetLength ()); // apply case-style
	_app_applycasestyle (path.GetBuffer (), path.GetLength ()); // apply case-style

	_r_str_alloc (&ptr_app->original_path, path.GetLength (), path);
	_r_str_alloc (&ptr_app->real_path, real_path.GetLength (), real_path);

	_app_getdisplayname (app_hash, ptr_app, &ptr_app->display_name);

	ptr_app->is_enabled = is_enabled;
	ptr_app->is_silent = is_silent;

	ptr_app->timestamp = timestamp ? timestamp : current_time;
	ptr_app->last_notify = last_notify;

	if (ptr_app->type == DataAppService || ptr_app->type == DataAppUWP)
		ptr_app->is_undeletable = true;

	// install timer
	if (timer)
	{
		if (timer > current_time)
		{
			if (!ptr_app->htimer && CreateTimerQueueTimer (&ptr_app->htimer, config.htimer, &_app_timer_callback, (PVOID)app_hash, DWORD ((timer - current_time) * _R_SECONDSCLOCK_MSEC), 0, WT_EXECUTEONLYONCE | WT_EXECUTEINTIMERTHREAD))
			{
				ptr_app->is_enabled = true;
				ptr_app->timer = timer;
			}
		}
		else
		{
			ptr_app->is_enabled = false;
		}
	}

	if (hwnd)
	{
		const UINT listview_id = _app_getlistview_id (ptr_app->type);

		if (listview_id)
		{
			const size_t item = _r_listview_getitemcount (hwnd, listview_id);

			_r_fastlock_acquireshared (&lock_checkbox);

			_r_listview_additem (hwnd, listview_id, item, 0, ptr_app->display_name, ptr_app->icon_id, _app_getappgroup (app_hash, ptr_app), app_hash);
			_app_setappiteminfo (hwnd, listview_id, item, app_hash, ptr_app);

			_r_fastlock_releaseshared (&lock_checkbox);
		}
	}

	return app_hash;
}

PITEM_APP _app_getapplication (size_t hash)
{
	if (hash && apps.find (hash) != apps.end ())
		return &apps.at (hash);

	return nullptr;
}

PITEM_RULE _app_getrule (size_t hash, EnumDataType type, BOOL is_readonly)
{
	if (!hash)
		return nullptr;

	for (size_t i = 0; i < rules_arr.size (); i++)
	{
		const PITEM_RULE ptr_rule = rules_arr.at (i);

		if (!ptr_rule || ptr_rule->type != type)
			continue;

		if (is_readonly != -1 && ((BOOL)ptr_rule->is_readonly != (is_readonly)))
			continue;

		if (ptr_rule->pname && _r_str_hash (ptr_rule->pname) == hash)
			return ptr_rule;
	}

	return nullptr;
}

bool _app_freeapplication (size_t hash)
{
	bool is_enabled = false;

	PITEM_APP ptr_app = _app_getapplication (hash);

	if (ptr_app)
	{
		is_enabled = ptr_app->is_enabled;

		SAFE_DELETE_ARRAY (ptr_app->display_name);
		SAFE_DELETE_ARRAY (ptr_app->real_path);
		SAFE_DELETE_ARRAY (ptr_app->original_path);

		SAFE_DELETE_ARRAY (ptr_app->description);
		SAFE_DELETE_ARRAY (ptr_app->signer);

		SAFE_DELETE_ARRAY (ptr_app->pdata);
	}

	if (hash)
	{
		_r_fastlock_acquireexclusive (&lock_cache);

		if (cache_signatures.find (hash) != cache_signatures.end ())
		{
			SAFE_DELETE_ARRAY (cache_signatures[hash]);

			cache_signatures.erase (hash);
		}

		if (cache_versions.find (hash) != cache_versions.end ())
		{
			SAFE_DELETE_ARRAY (cache_versions[hash]);

			cache_versions.erase (hash);
		}

		_r_fastlock_releaseexclusive (&lock_cache);

		for (size_t i = 0; i < rules_arr.size (); i++)
		{
			PITEM_RULE ptr_rule = rules_arr.at (i);

			if (ptr_rule)
			{
				if (ptr_rule->type != DataRuleCustom)
					continue;

				if (ptr_rule->apps.find (hash) != ptr_rule->apps.end ())
				{
					ptr_rule->apps.erase (hash);

					if (!is_enabled && ptr_rule->is_enabled)
						is_enabled = true;

					if (ptr_rule->is_enabled && ptr_rule->apps.empty ())
					{
						ptr_rule->is_enabled = false;
						ptr_rule->is_haveerrors = false;
					}

					const UINT rule_listview_id = _app_getlistview_id (ptr_rule->type);

					_r_fastlock_acquireshared (&lock_checkbox);
					_app_setruleiteminfo (app.GetHWND (), rule_listview_id, _app_getposition (app.GetHWND (), rule_listview_id, i), ptr_rule, false);
					_r_fastlock_releaseshared (&lock_checkbox);
				}
			}
		}

		_r_fastlock_acquireexclusive (&lock_notification);
		_app_freenotify (hash, false, true);
		_r_fastlock_releaseexclusive (&lock_notification);

		if (ptr_app && ptr_app->htimer)
			DeleteTimerQueueTimer (config.htimer, ptr_app->htimer, nullptr);

		apps.erase (hash);
	}

	return is_enabled;
}

void _app_freerule (PITEM_RULE * ptr)
{
	if (ptr)
	{
		PITEM_RULE ptr_rule = *ptr;

		SAFE_DELETE (ptr_rule);

		*ptr = nullptr;
	}
}

void _app_getcount (PITEM_STATUS ptr_status)
{
	if (!ptr_status)
		return;

	SecureZeroMemory (ptr_status, sizeof (ITEM_STATUS));

	for (auto const &p : apps)
	{
		const ITEM_APP* ptr_app = &p.second;
		const bool is_used = _app_isappused (ptr_app, p.first);

		if (_app_istimeractive (ptr_app))
			ptr_status->apps_timer_count += 1;

		if (!ptr_app->is_undeletable && (!_app_isappexists (ptr_app) || !is_used) && !(ptr_app->type == DataAppService || ptr_app->type == DataAppUWP))
			ptr_status->apps_unused_count += 1;

		if (is_used)
			ptr_status->apps_count += 1;
	}

	for (size_t i = 0; i < rules_arr.size (); i++)
	{
		const PITEM_RULE ptr_rule = rules_arr.at (i);

		if (!ptr_rule || ptr_rule->type != DataRuleCustom)
			continue;

		if (ptr_rule->is_enabled && !ptr_rule->apps.empty ())
			ptr_status->rules_global_count += 1;

		if (ptr_rule->is_readonly)
			ptr_status->rules_predefined_count += 1;

		else if (!ptr_rule->is_readonly)
			ptr_status->rules_user_count += 1;

		ptr_status->rules_count += 1;
	}
}

size_t _app_getappgroup (size_t hash, PITEM_APP const ptr_app)
{
	//	if(!app.ConfigGet (L"IsEnableGroups", false).AsBool ())
	//		return LAST_VALUE;

	if (!ptr_app)
		return 2;

	// apps with special rule
	if (app.ConfigGet (L"IsEnableSpecialGroup", true).AsBool () && _app_isapphaverule (hash))
		return 1;

	return ptr_app->is_enabled ? 0 : 2;
}

size_t _app_getrulegroup (PITEM_RULE const ptr_rule)
{
	//	if(!app.ConfigGet (L"IsEnableGroups", false).AsBool ())
	//		return LAST_VALUE;

	if (!ptr_rule || !ptr_rule->is_enabled)
		return 2;

	if (app.ConfigGet (L"IsEnableSpecialGroup", true).AsBool () && (ptr_rule->is_forservices || !ptr_rule->apps.empty ()))
		return 1;

	return 0;
}

size_t _app_getruleicon (PITEM_RULE const ptr_rule)
{
	if (!ptr_rule)
		return 0;

	return ptr_rule->is_block ? 1 : 0;
}

rstring _app_gettooltip (UINT listview_id, size_t idx)
{
	rstring result;

	if (
		listview_id == IDC_APPS_LV ||
		listview_id == IDC_APPS_PROFILE ||
		listview_id == IDC_APPS_SERVICE ||
		listview_id == IDC_APPS_UWP ||
		listview_id == IDC_NETWORK
		)
	{
		if (listview_id == IDC_NETWORK)
		{
			_r_fastlock_acquireshared (&lock_network);

			const PITEM_NETWORK ptr_network = network_arr.at (idx);

			if (ptr_network)
				idx = ptr_network->hash;

			_r_fastlock_releaseshared (&lock_network);
		}

		PITEM_APP ptr_app = _app_getapplication (idx);

		if (ptr_app)
		{
			result = (ptr_app->real_path && ptr_app->real_path[0]) ? ptr_app->real_path : ((ptr_app->display_name && ptr_app->display_name[0]) ? ptr_app->display_name : ptr_app->original_path);

			// file information
			if (ptr_app->type == DataAppRegular)
			{
				rstring display_name;

				if (_app_getversioninfo (idx, ptr_app->real_path, &ptr_app->description))
				{
					display_name = ptr_app->description;

					if (!display_name.IsEmpty ())
						result.AppendFormat (L"\r\n%s:\r\n" SZ_TAB L"%s" SZ_TAB, app.LocaleString (IDS_FILE, nullptr).GetString (), display_name.GetString ());
				}
			}
			else if (ptr_app->type == DataAppService)
			{
				rstring display_name;

				if (_app_item_get (ptr_app->type, idx, &display_name, nullptr, nullptr, nullptr))
					result.AppendFormat (L"\r\n%s:\r\n" SZ_TAB L"%s\r\n" SZ_TAB L"%s" SZ_TAB, app.LocaleString (IDS_FILE, nullptr).GetString (), ptr_app->original_path, display_name.GetString ());
			}
			else if (ptr_app->type == DataAppUWP)
			{
				rstring display_name;

				if (_app_item_get (ptr_app->type, idx, &display_name, nullptr, nullptr, nullptr))
					result.AppendFormat (L"\r\n%s:\r\n" SZ_TAB L"%s" SZ_TAB, app.LocaleString (IDS_FILE, nullptr).GetString (), display_name.GetString ());
			}

			// signature information
			if (app.ConfigGet (L"IsCertificatesEnabled", false).AsBool () && ptr_app->is_signed && ptr_app->signer)
				result.AppendFormat (L"\r\n%s:\r\n" SZ_TAB L"%s", app.LocaleString (IDS_SIGNATURE, nullptr).GetString (), ptr_app->signer);

			// timer information
			if (_app_istimeractive (ptr_app))
				result.AppendFormat (L"\r\n%s:\r\n" SZ_TAB L"%s", app.LocaleString (IDS_TIMELEFT, nullptr).GetString (), _r_fmt_interval (ptr_app->timer - _r_unixtime_now (), 3).GetString ());

			// notes
			{
				rstring buffer;

				if (!_app_isappexists (ptr_app))
					buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_INVALID, nullptr).GetString ());

				if (listview_id != IDC_NETWORK && _app_isapphaveconnection (idx))
					buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_CONNECTION, nullptr).GetString ());

				if (ptr_app->is_silent)
					buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_SILENT, nullptr).GetString ());

				if (ptr_app->is_system)
					buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_SYSTEM, nullptr).GetString ());

				// app type
				{
					if (ptr_app->type == DataAppNetwork)
						buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_NETWORK, nullptr).GetString ());

					else if (ptr_app->type == DataAppPico)
						buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_PICO, nullptr).GetString ());

					else if (ptr_app->type == DataAppUWP)
						buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_PACKAGE, nullptr).GetString ());

					else if (ptr_app->type == DataAppService)
						buffer.AppendFormat (SZ_TAB L"%s\r\n", app.LocaleString (IDS_HIGHLIGHT_SERVICE, nullptr).GetString ());
				}

				if (!buffer.IsEmpty ())
				{
					buffer.InsertFormat (0, L"\r\n%s:\r\n", app.LocaleString (IDS_NOTES, nullptr).GetString ());
					result.Append (buffer);
				}
			}
		}
	}
	else if (
		listview_id == IDC_RULES_BLOCKLIST ||
		listview_id == IDC_RULES_SYSTEM ||
		listview_id == IDC_RULES_CUSTOM
		)
	{
		const PITEM_RULE ptr_rule = rules_arr.at (idx);

		if (ptr_rule)
		{
			rstring rule_remote = ptr_rule->prule_remote;
			rstring rule_local = ptr_rule->prule_local;

			rule_remote = rule_remote.IsEmpty () ? app.LocaleString (IDS_STATUS_EMPTY, nullptr) : rule_remote.Replace (RULE_DELIMETER, L"\r\n" SZ_TAB);
			rule_local = rule_local.IsEmpty () ? app.LocaleString (IDS_STATUS_EMPTY, nullptr) : rule_local.Replace (RULE_DELIMETER, L"\r\n" SZ_TAB);

			result.Format (L"%s (#%d)\r\n%s:\r\n%s%s\r\n%s:\r\n%s%s", ptr_rule->pname, idx, app.LocaleString (IDS_RULE, L" (" SZ_LOG_REMOTE_ADDRESS L")").GetString (), SZ_TAB, rule_remote.GetString (), app.LocaleString (IDS_RULE, L" (" SZ_LOG_LOCAL_ADDRESS L")").GetString (), SZ_TAB, rule_local.GetString ());

			if (ptr_rule->is_forservices || !ptr_rule->apps.empty ())
				result.AppendFormat (L"\r\n%s:\r\n%s%s", app.LocaleString (IDS_FILEPATH, nullptr).GetString (), SZ_TAB, _app_rulesexpand (ptr_rule, true, L"\r\n" SZ_TAB).GetString ());
		}
	}

	return result;
}

void _app_setappiteminfo (HWND hwnd, UINT listview_id, size_t item, size_t app_hash, PITEM_APP ptr_app)
{
	if (!ptr_app || !listview_id || item == LAST_VALUE)
		return;

	_app_getappicon (ptr_app, true, &ptr_app->icon_id, nullptr);

	_r_listview_setitem (hwnd, listview_id, item, 0, ptr_app->display_name, ptr_app->icon_id, _app_getappgroup (app_hash, ptr_app));
	_r_listview_setitem (hwnd, listview_id, item, 1, _r_fmt_date (ptr_app->timestamp, FDTF_SHORTDATE | FDTF_SHORTTIME));

	_r_listview_setitemcheck (hwnd, listview_id, item, ptr_app->is_enabled);
}

void _app_setruleiteminfo (HWND hwnd, UINT listview_id, size_t item, PITEM_RULE ptr_rule, bool include_apps)
{
	if (!ptr_rule || !listview_id || item == LAST_VALUE)
		return;

	_r_listview_setitem (hwnd, listview_id, item, 0, ptr_rule->type == DataRuleCustom && ptr_rule->is_readonly ? _r_fmt (L"%s*", ptr_rule->pname) : ptr_rule->pname, _app_getruleicon (ptr_rule), _app_getrulegroup (ptr_rule));
	_r_listview_setitem (hwnd, listview_id, item, 1, app.LocaleString (IDS_DIRECTION_1 + ptr_rule->dir, nullptr));
	_r_listview_setitem (hwnd, listview_id, item, 2, ptr_rule->protocol ? _app_getprotoname (ptr_rule->protocol) : app.LocaleString (IDS_ALL, nullptr));

	_r_listview_setitemcheck (hwnd, listview_id, item, ptr_rule->is_enabled);

	if (include_apps)
	{
		for (auto &p : ptr_rule->apps)
		{
			const size_t app_hash = p.first;
			const PITEM_APP ptr_app = _app_getapplication (app_hash);

			if (ptr_app)
			{
				const UINT app_listview_id = _app_getlistview_id (ptr_app->type);

				_app_setappiteminfo (hwnd, app_listview_id, _app_getposition (hwnd, app_listview_id, app_hash), app_hash, ptr_app);
			}
		}
	}
}

void _app_ruleenable (PITEM_RULE ptr_rule, bool is_enable)
{
	if (!ptr_rule)
		return;

	ptr_rule->is_enabled = is_enable;

	if (ptr_rule->is_readonly && ptr_rule->pname)
	{
		const size_t rule_hash = _r_str_hash (ptr_rule->pname);

		if (rule_hash)
		{
			if (rules_config.find (rule_hash) != rules_config.end ())
			{
				PITEM_RULE_CONFIG* ptr_config = &rules_config[rule_hash];

				if (!*ptr_config)
					* ptr_config = new ITEM_RULE_CONFIG;

				(*ptr_config)->is_enabled = is_enable;
			}
			else
			{
				PITEM_RULE_CONFIG ptr_config = new ITEM_RULE_CONFIG;

				ptr_config->is_enabled = is_enable;

				rules_config[rule_hash] = ptr_config;
			}
		}
	}
}

void _app_ruleblocklistset ()
{
	const bool is_blockspy = app.ConfigGet (L"BlocklistBlockSpy", true).AsBool ();
	const bool is_allowupdate = app.ConfigGet (L"BlocklistAllowUpdate", false).AsBool ();
	const bool is_allowextra = app.ConfigGet (L"BlocklistAllowExtra", false).AsBool ();

	for (size_t i = 0; i < rules_arr.size (); i++)
	{
		PITEM_RULE ptr_rule = rules_arr.at (i);

		if (!ptr_rule || !ptr_rule->pname || !ptr_rule->pname[0] || ptr_rule->type != DataRuleBlocklist)
			continue;

		if (_wcsnicmp (ptr_rule->pname, L"spy_", 4) == 0)
		{
			ptr_rule->is_block = true;
			ptr_rule->is_enabled = is_blockspy;
		}
		else if (_wcsnicmp (ptr_rule->pname, L"update_", 7) == 0)
		{
			ptr_rule->is_block = !is_allowupdate;
			ptr_rule->is_enabled = true;
		}
		else if (_wcsnicmp (ptr_rule->pname, L"extra_", 6) == 0)
		{
			ptr_rule->is_block = !is_allowextra;
			ptr_rule->is_enabled = true;
		}
	}
}

rstring _app_rulesexpand (PITEM_RULE const ptr_rule, bool is_forservices, LPCWSTR delimeter)
{
	rstring result;

	if (ptr_rule)
	{
		if (is_forservices && ptr_rule->is_forservices)
		{
			rstring svchost_path = _r_path_expand (PATH_SVCHOST);

			_app_applycasestyle (svchost_path.GetBuffer (), svchost_path.GetLength ());
			svchost_path.ReleaseBuffer ();

			result.AppendFormat (L"%s%s", PROC_SYSTEM_NAME, delimeter);
			result.AppendFormat (L"%s%s", svchost_path.GetString (), delimeter);
		}

		for (auto const &p : ptr_rule->apps)
		{
			ITEM_APP const* ptr_app = _app_getapplication (p.first);

			if (ptr_app)
			{
				if (ptr_app->type == DataAppUWP || ptr_app->type == DataAppService)
				{
					if (ptr_app->display_name && ptr_app->display_name[0])
						result.Append (ptr_app->display_name);
				}
				else
				{
					if (ptr_app->original_path && ptr_app->original_path[0])
						result.Append (ptr_app->original_path);
				}

				result.Append (delimeter);
			}
		}

		result.Trim (delimeter);
	}

	return result;
}

bool _app_isapphaveconnection (size_t app_hash)
{
	if (!app_hash)
		return false;

	for (size_t i = 0; i < network_arr.size (); i++)
	{
		const PITEM_NETWORK ptr_network = network_arr.at (i);

		if (ptr_network && ptr_network->hash == app_hash)
		{
			if ((ptr_network->protocol == IPPROTO_TCP && (ptr_network->state == MIB_TCP_STATE_ESTAB)) || (ptr_network->protocol == IPPROTO_UDP && !ptr_network->state))
				return true;
		}
	}

	return false;
}

bool _app_isapphaverule (size_t app_hash)
{
	if (!app_hash)
		return false;

	for (size_t i = 0; i < rules_arr.size (); i++)
	{
		PITEM_RULE const ptr_rule = rules_arr.at (i);

		if (ptr_rule && ptr_rule->is_enabled && ptr_rule->type == DataRuleCustom && ptr_rule->apps.find (app_hash) != ptr_rule->apps.end ())
			return true;
	}

	return false;
}

bool _app_isappused (ITEM_APP const* ptr_app, size_t app_hash)
{
	if (ptr_app && (ptr_app->is_enabled || ptr_app->is_silent) || _app_isapphaverule (app_hash))
		return true;

	return false;
}

bool _app_isappexists (ITEM_APP const* ptr_app)
{
	if (!ptr_app)
		return false;

	if (ptr_app->is_undeletable)
		return true;

	if (ptr_app->is_enabled && ptr_app->is_haveerrors)
		return false;

	if (ptr_app->type == DataAppDevice || ptr_app->type == DataAppNetwork || ptr_app->type == DataAppPico)
		return true;

	else if (ptr_app->type == DataAppRegular)
		return ptr_app->real_path && ptr_app->real_path[0] && _r_fs_exists (ptr_app->real_path);

	else if (ptr_app->type == DataAppService || ptr_app->type == DataAppUWP)
		return _app_item_get (ptr_app->type, _r_str_hash (ptr_app->original_path), nullptr, nullptr, nullptr, nullptr);

	return true;
}

bool _app_isruleblocklist (LPCWSTR name)
{
	if (!name || !name[0])
		return false;

	if (
		_wcsnicmp (name, L"extra_", 6) == 0 ||
		_wcsnicmp (name, L"spy_", 4) == 0 ||
		_wcsnicmp (name, L"update_", 7) == 0
		)
		return true;

	return false;
}

bool _app_isrulehost (LPCWSTR rule)
{
	if (!rule || !rule[0])
		return false;

	NET_ADDRESS_INFO ni;
	SecureZeroMemory (&ni, sizeof (ni));

	USHORT port = 0;
	BYTE prefix_length = 0;

	static const DWORD types = NET_STRING_NAMED_ADDRESS | NET_STRING_NAMED_SERVICE;
	const DWORD errcode = ParseNetworkString (rule, types, &ni, &port, &prefix_length);

	return (errcode == ERROR_SUCCESS);
}

bool _app_isruleip (LPCWSTR rule)
{
	if (!rule || !rule[0])
		return false;

	NET_ADDRESS_INFO ni;
	SecureZeroMemory (&ni, sizeof (ni));

	USHORT port = 0;
	BYTE prefix_length = 0;

	static const DWORD types = NET_STRING_IP_ADDRESS | NET_STRING_IP_SERVICE | NET_STRING_IP_NETWORK | NET_STRING_IP_ADDRESS_NO_SCOPE | NET_STRING_IP_SERVICE_NO_SCOPE;
	const DWORD errcode = ParseNetworkString (rule, types, &ni, &port, &prefix_length);

	return (errcode == ERROR_SUCCESS);
}

bool _app_isruleport (LPCWSTR rule)
{
	if (!rule || !rule[0])
		return false;

	const size_t length = _r_str_length (rule);

	for (size_t i = 0; i < length; i++)
	{
		if (iswdigit (rule[i]) == 0 && rule[i] != RULE_RANGE_CHAR)
			return false;
	}

	return true;
}

//bool _app_isrulepresent (size_t hash)
//{
//	if (!hash)
//		return false;
//
//	for (size_t i = 0; i < rules_arr.size (); i++)
//	{
//		PITEM_RULE ptr_rule = rules_arr.at (i);
//
//		if (ptr_rule && (ptr_rule->type == DataRuleSystem || (ptr_rule->type == DataRuleCustom && ptr_rule->is_readonly)) && ptr_rule->pname && _r_str_hash (ptr_rule->pname) == hash)
//			return true;
//	}
//
//	return false;
//}

bool _app_profile_load_check_node (pugi::xml_node & root, EnumXmlType type, bool is_strict)
{
	if (root)
	{
		if (is_strict)
			return (root.attribute (L"type").as_int () == type);

		return (root.attribute (L"type").empty () || (root.attribute (L"type").as_int () == type));
	}

	return false;
}

bool _app_profile_load_check (LPCWSTR path, EnumXmlType type, bool is_strict)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file (path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

	if (result)
	{
		pugi::xml_node root = doc.child (L"root");

		return _app_profile_load_check_node (root, type, is_strict);
	}

	return false;
}

void _app_profile_load_fallback ()
{
	if (!_app_getapplication (config.myhash))
		_app_addapplication (nullptr, app.GetBinaryPath (), 0, 0, 0, false, true, true);

	// disable deletion for this shit ;)
	if (!_app_getapplication (config.ntoskrnl_hash))
		_app_addapplication (nullptr, PROC_SYSTEM_NAME, 0, 0, 0, false, false, true);

	if (!_app_getapplication (config.svchost_hash))
		_app_addapplication (nullptr, _r_path_expand (PATH_SVCHOST), 0, 0, 0, false, false, true);

	apps[config.myhash].is_undeletable = true;
	apps[config.ntoskrnl_hash].is_undeletable = true;
	apps[config.svchost_hash].is_undeletable = true;
}

void _app_profile_load_helper (pugi::xml_node & root, EnumDataType type)
{
	for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
	{
		if (type == DataAppRegular)
		{
			_r_fastlock_acquireexclusive (&lock_access);
			_app_addapplication (nullptr, item.attribute (L"path").as_string (), item.attribute (L"timestamp").as_llong (), item.attribute (L"timer").as_llong (), item.attribute (L"last_notify").as_llong (), item.attribute (L"is_silent").as_bool (), item.attribute (L"is_enabled").as_bool (), true);
			_r_fastlock_releaseexclusive (&lock_access);
		}
		else if (type == DataRuleBlocklist || type == DataRuleSystem || type == DataRuleCustom)
		{
			const bool is_internal = (type == DataRuleSystem);

			PITEM_RULE ptr_rule = new ITEM_RULE;

			const size_t rule_hash = _r_str_hash (item.attribute (L"name").as_string ());
			PITEM_RULE_CONFIG ptr_config = nullptr;

			// allocate required memory
			{
				const rstring attr_name = item.attribute (L"name").as_string ();
				const rstring attr_rule_remote = item.attribute (L"rule").as_string ();
				const rstring attr_rule_local = item.attribute (L"rule_local").as_string ();

				const size_t name_length = min (attr_name.GetLength (), RULE_NAME_CCH_MAX);
				const size_t rule_remote_length = min (attr_rule_remote.GetLength (), RULE_RULE_CCH_MAX);
				const size_t rule_local_length = min (attr_rule_local.GetLength (), RULE_RULE_CCH_MAX);

				_r_str_alloc (&ptr_rule->pname, name_length, attr_name);
				_r_str_alloc (&ptr_rule->prule_remote, rule_remote_length, attr_rule_remote);
				_r_str_alloc (&ptr_rule->prule_local, rule_local_length, attr_rule_local);
			}

			ptr_rule->dir = (FWP_DIRECTION)item.attribute (L"dir").as_uint ();
			ptr_rule->protocol = (UINT8)item.attribute (L"protocol").as_uint ();
			ptr_rule->af = (ADDRESS_FAMILY)item.attribute (L"version").as_uint ();

			ptr_rule->type = ((is_internal && item.attribute (L"is_custom").as_bool ()) ? DataRuleCustom : type);
			ptr_rule->is_block = item.attribute (L"is_block").as_bool ();
			ptr_rule->is_forservices = item.attribute (L"is_services").as_bool ();
			ptr_rule->is_readonly = (type != DataRuleCustom);

			// calculate rule weight
			if (type == DataRuleBlocklist)
				ptr_rule->weight = FILTER_WEIGHT_BLOCKLIST;

			else if (type == DataRuleSystem)
				ptr_rule->weight = FILTER_WEIGHT_SYSTEM;

			else if (type == DataRuleCustom)
				ptr_rule->weight = ptr_rule->is_block ? FILTER_WEIGHT_CUSTOM_BLOCK : FILTER_WEIGHT_CUSTOM;

			ptr_rule->is_enabled = item.attribute (L"is_enabled").as_bool ();

			if (is_internal)
			{
				// internal rules
				if (rules_config.find (rule_hash) != rules_config.end ())
				{
					ptr_config = rules_config[rule_hash];

					if (ptr_config)
						ptr_rule->is_enabled = ptr_config->is_enabled;
				}
				else
				{
					ptr_config = new ITEM_RULE_CONFIG;

					ptr_config->is_enabled = ptr_rule->is_enabled;

					_r_str_alloc (&ptr_config->pname, _r_str_length (ptr_rule->pname), ptr_rule->pname);

					_r_fastlock_acquireexclusive (&lock_access);
					rules_config[rule_hash] = ptr_config;
					_r_fastlock_releaseexclusive (&lock_access);
				}
			}

			// load apps
			{
				rstring apps_rule = item.attribute (L"apps").as_string ();

				if (is_internal && ptr_config && ptr_config->papps && ptr_config->papps[0])
				{
					if (apps_rule.IsEmpty ())
						apps_rule = ptr_config->papps;

					else
						apps_rule.AppendFormat (L"%s%s", RULE_DELIMETER, ptr_config->papps);
				}

				if (!apps_rule.IsEmpty ())
				{
					rstring::rvector arr = apps_rule.AsVector (RULE_DELIMETER);

					for (size_t i = 0; i < arr.size (); i++)
					{
						const rstring app_path = _r_path_expand (arr.at (i).Trim (L"\r\n "));
						size_t app_hash = app_path.Hash ();

						if (app_hash)
						{
							if (item.attribute (L"is_services").as_bool () && (app_hash == config.ntoskrnl_hash || app_hash == config.svchost_hash))
								continue;

							if (!_app_getapplication (app_hash))
							{
								_r_fastlock_acquireexclusive (&lock_access);
								app_hash = _app_addapplication (nullptr, app_path, 0, 0, 0, false, false, true);
								_r_fastlock_releaseexclusive (&lock_access);
							}

							if (ptr_rule->type == DataRuleSystem)
								apps[app_hash].is_undeletable = true;

							ptr_rule->apps[app_hash] = true;
						}
					}
				}
			}

			_r_fastlock_acquireexclusive (&lock_access);
			rules_arr.push_back (ptr_rule);
			_r_fastlock_releaseexclusive (&lock_access);
		}
		else if (type == DataRulesConfig)
		{
			if (item.attribute (L"name").empty ())
				continue;

			const rstring rule_name = item.attribute (L"name").as_string ();

			// do not load blocklist config (old versions hack)!
			if (_app_isruleblocklist (rule_name))
				continue;

			const size_t rule_hash = rule_name.Hash ();

			if (rules_config.find (rule_hash) == rules_config.end ())
			{
				PITEM_RULE_CONFIG ptr_config = new ITEM_RULE_CONFIG;

				ptr_config->is_enabled = item.attribute (L"is_enabled").as_bool ();

				const rstring attr_name = item.attribute (L"name").as_string ();
				const rstring attr_apps = item.attribute (L"apps").as_string ();

				_r_str_alloc (&ptr_config->pname, attr_name.GetLength (), attr_name);
				_r_str_alloc (&ptr_config->papps, attr_apps.GetLength (), attr_apps);

				_r_fastlock_acquireexclusive (&lock_access);
				rules_config[rule_hash] = ptr_config;
				_r_fastlock_releaseexclusive (&lock_access);
			}
		}
	}
}

void _app_profile_load_internal (LPCWSTR path, LPCWSTR path_backup, time_t * ptimestamp)
{
	pugi::xml_document doc_original;
	pugi::xml_document doc_backup;

	pugi::xml_parse_result load_original = doc_original.load_file (path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);
	pugi::xml_parse_result load_backup;

	pugi::xml_node root;

	// if file not found or parsing error, load from backup
	if (path_backup)
	{
		DWORD size = 0;
		const LPVOID buffer = _app_loadresource (app.GetHINSTANCE (), path_backup, RT_RCDATA, &size);

		if (buffer)
			load_backup = doc_backup.load_buffer (buffer, size, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

		if (load_backup)
		{
			if (!load_original || doc_backup.child (L"root").attribute (L"timestamp").as_llong () > doc_original.child (L"root").attribute (L"timestamp").as_llong ())
			{
				root = doc_backup.child (L"root");
				load_original = load_backup;
			}
			else
			{
				root = doc_original.child (L"root");
			}
		}
	}

	// show only syntax, memory and i/o errors...
	if (!load_original && load_original.status != pugi::status_file_not_found && load_original.status != pugi::status_no_document_element)
		_app_logerror (L"pugi::load_file", 0, _r_fmt (L"status: %d,offset: %d,text: %s,file: %s", load_original.status, load_original.offset, rstring (load_original.description ()).GetString (), path), false);

	if (load_original && root)
	{
		if (_app_profile_load_check_node (root, XmlProfileInternalV3, true))
		{
			if (ptimestamp)
				*ptimestamp = root.attribute (L"timestamp").as_llong ();

			pugi::xml_node root_rules_blocklist = root.child (L"rules_blocklist");

			if (root_rules_blocklist)
				_app_profile_load_helper (root_rules_blocklist, DataRuleBlocklist);

			pugi::xml_node root_rules_system = root.child (L"rules_system");

			if (root_rules_system)
				_app_profile_load_helper (root_rules_system, DataRuleSystem);
		}
	}
}

void _app_profile_load (HWND hwnd, LPCWSTR path_custom)
{
	const UINT current_listview_id = _app_gettab_id (hwnd);
	const size_t selected_item = (size_t)SendDlgItemMessage (hwnd, current_listview_id, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
	const INT scroll_pos = GetScrollPos (GetDlgItem (hwnd, current_listview_id), SB_VERT);

	// clean listview
	if (hwnd)
	{
		_r_listview_deleteallitems (hwnd, IDC_APPS_PROFILE);
		_r_listview_deleteallitems (hwnd, IDC_APPS_SERVICE);
		_r_listview_deleteallitems (hwnd, IDC_APPS_UWP);
		_r_listview_deleteallitems (hwnd, IDC_RULES_BLOCKLIST);
		_r_listview_deleteallitems (hwnd, IDC_RULES_SYSTEM);
		_r_listview_deleteallitems (hwnd, IDC_RULES_CUSTOM);
	}

	_r_fastlock_acquireexclusive (&lock_access);

	// clear services/uwp apps
	_app_freearray (&items);

	// clear apps
	apps.clear ();

	// clear rules
	{
		for (auto &p : rules_arr)
			SAFE_DELETE (p);

		rules_arr.clear ();
	}

	// clear rules config
	{
		for (auto &p : rules_config)
			SAFE_DELETE (p.second);

		rules_config.clear ();
	}

	// generate package list (win8+)
	if (_r_sys_validversion (6, 2))
		_app_generate_packages ();

	// generate services list
	_app_generate_services ();

	_r_fastlock_releaseexclusive (&lock_access);

	// load profile
	if (path_custom || _r_fs_exists (config.profile_path) || _r_fs_exists (config.profile_path_backup))
	{
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file (path_custom ? path_custom : config.profile_path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

		// load backup
		if (!result)
			result = doc.load_file (config.profile_path_backup, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

		if (!result)
		{
			// show only syntax, memory and i/o errors...
			if (result.status != pugi::status_file_not_found && result.status != pugi::status_no_document_element)
				_app_logerror (L"pugi::load_file", 0, _r_fmt (L"status: %d,offset: %d,text: %s,file: %s", result.status, result.offset, rstring (result.description ()).GetString (), path_custom ? path_custom : config.profile_path), false);

			_r_fastlock_acquireexclusive (&lock_access);
			_app_profile_load_fallback ();
			_r_fastlock_releaseexclusive (&lock_access);
		}
		else
		{
			pugi::xml_node root = doc.child (L"root");

			if (root)
			{
				if (_app_profile_load_check_node (root, XmlProfileV3, true))
				{
					// load apps (new!)
					pugi::xml_node root_apps = root.child (L"apps");

					if (root_apps)
						_app_profile_load_helper (root_apps, DataAppRegular);

					_r_fastlock_acquireexclusive (&lock_access);
					_app_profile_load_fallback ();
					_r_fastlock_releaseexclusive (&lock_access);

					// load rules config (new!)
					pugi::xml_node root_rules_config = root.child (L"rules_config");

					if (root_rules_config)
						_app_profile_load_helper (root_rules_config, DataRulesConfig);

					// load user rules (new!)
					pugi::xml_node root_rules_custom = root.child (L"rules_custom");

					if (root_rules_custom)
						_app_profile_load_helper (root_rules_custom, DataRuleCustom);

					// load internal rules (new!)
					_app_profile_load_internal (config.profile_internal_path, MAKEINTRESOURCE (IDR_PROFILE_INTERNAL), &config.profile_internal_timestamp);
				}
			}
		}
	}
	else
	{
		// load apps (old!)
		{
			pugi::xml_document doc_apps;
			pugi::xml_parse_result result_apps = doc_apps.load_file (config.apps_path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

			// load backup
			if (!result_apps)
				result_apps = doc_apps.load_file (config.apps_path_backup, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

			if (result_apps)
			{
				pugi::xml_node root = doc_apps.child (L"root");

				if (root)
				{
					if (_app_profile_load_check_node (root, XmlApps, false))
						_app_profile_load_helper (root, DataAppRegular);
				}

				// made old config backup!
				if (_r_fs_exists (config.apps_path))
					_r_fs_move (config.apps_path, config.apps_path_backup, MOVEFILE_REPLACE_EXISTING);
			}

			_r_fastlock_acquireexclusive (&lock_access);
			_app_profile_load_fallback ();
			_r_fastlock_releaseexclusive (&lock_access);
		}

		// load rules config (old!)
		{
			pugi::xml_document doc_rules_config;
			pugi::xml_parse_result result_rules_config = doc_rules_config.load_file (config.rules_config_path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

			// load backup
			if (!result_rules_config)
				result_rules_config = doc_rules_config.load_file (config.rules_config_path_backup, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

			if (result_rules_config)
			{
				pugi::xml_node root = doc_rules_config.child (L"root");

				if (root)
				{
					if (_app_profile_load_check_node (root, XmlRulesConfig, false))
						_app_profile_load_helper (root, DataRulesConfig);
				}

				// made old config backup!
				if (_r_fs_exists (config.rules_config_path))
					_r_fs_move (config.rules_config_path, config.rules_config_path_backup, MOVEFILE_REPLACE_EXISTING);
			}
		}

		// load internal rules (new!)
		_app_profile_load_internal (config.profile_internal_path, MAKEINTRESOURCE (IDR_PROFILE_INTERNAL), &config.profile_internal_timestamp);

		// load rules custom (old!)
		{
			pugi::xml_document doc_rules_custom;
			pugi::xml_parse_result result_rules_custom = doc_rules_custom.load_file (config.rules_custom_path, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);

			// load backup
			if (!result_rules_custom)
			{
				if (_r_fs_exists (config.rules_custom_path_backup))
					result_rules_custom = doc_rules_custom.load_file (config.rules_custom_path_backup, PUGIXML_LOAD_FLAGS, PUGIXML_LOAD_ENCODING);
			}

			if (result_rules_custom)
			{
				pugi::xml_node root = doc_rules_custom.child (L"root");

				if (root)
				{
					if (_app_profile_load_check_node (root, XmlRules, false))
						_app_profile_load_helper (root, DataRuleCustom);
				}

				// made old config backup!
				if (_r_fs_exists (config.rules_custom_path))
					_r_fs_move (config.rules_custom_path, config.rules_custom_path_backup, MOVEFILE_REPLACE_EXISTING);
			}
		}

		_app_profile_save (); // required!
	}

	_r_fastlock_acquireexclusive (&lock_access);
	_app_ruleblocklistset ();
	_r_fastlock_releaseexclusive (&lock_access);

	if (hwnd)
	{
		_r_fastlock_acquireshared (&lock_access);

		// add apps
		for (auto &p : apps)
		{
			const size_t app_hash = p.first;
			const PITEM_APP ptr_app = &p.second;

			const UINT listview_id = _app_getlistview_id (ptr_app->type);

			if (!listview_id)
				continue;

			const size_t item = _r_listview_getitemcount (hwnd, listview_id);

			_r_fastlock_acquireshared (&lock_checkbox);

			_r_listview_additem (hwnd, listview_id, item, 0, ptr_app->display_name, ptr_app->icon_id, _app_getappgroup (app_hash, ptr_app), app_hash);
			_app_setappiteminfo (hwnd, listview_id, item, app_hash, ptr_app);

			_r_fastlock_releaseshared (&lock_checkbox);
		}

		// add services and store apps
		for (size_t i = 0; i < items.size (); i++)
		{
			const PITEM_ADD ptr_item = items.at (i);

			if (ptr_item)
				_app_addapplication (hwnd, ptr_item->type == DataAppService ? ptr_item->service_name : ptr_item->sid, ptr_item->timestamp, 0, 0, false, false, true);
		}

		// add rules
		for (size_t i = 0; i < rules_arr.size (); i++)
		{
			const PITEM_RULE ptr_rule = rules_arr.at (i);

			if (!ptr_rule)
				continue;

			const UINT listview_id = _app_getlistview_id (ptr_rule->type);

			if (!listview_id)
				continue;

			const size_t item = _r_listview_getitemcount (hwnd, listview_id);

			_r_fastlock_acquireshared (&lock_checkbox);

			_r_listview_additem (hwnd, listview_id, item, 0, ptr_rule->pname, _app_getruleicon (ptr_rule), _app_getrulegroup (ptr_rule), i);
			_app_setruleiteminfo (hwnd, listview_id, item, ptr_rule, false);

			_r_fastlock_releaseshared (&lock_checkbox);
		}

		_r_fastlock_releaseshared (&lock_access);
	}

	if (hwnd && current_listview_id)
	{
		const UINT new_listview_id = _app_gettab_id (hwnd);

		_app_listviewsort (hwnd, new_listview_id);

		if (current_listview_id == new_listview_id)
			_app_showitem (hwnd, current_listview_id, selected_item, scroll_pos);
	}
}

void _app_profile_save (LPCWSTR path_custom)
{
	const time_t current_time = _r_unixtime_now ();
	const bool is_backuprequired = !path_custom && (app.ConfigGet (L"IsBackupProfile", true).AsBool () && (((current_time - app.ConfigGet (L"BackupTimestamp", 0).AsLonglong ()) >= app.ConfigGet (L"BackupPeriod", _R_SECONDSCLOCK_HOUR (BACKUP_HOURS_PERIOD)).AsLonglong ()) || !_r_fs_exists (config.profile_path_backup)));

	pugi::xml_document doc;
	pugi::xml_node root = doc.append_child (L"root");

	if (root)
	{
		root.append_attribute (L"timestamp").set_value (current_time);
		root.append_attribute (L"type").set_value (XmlProfileV3);

		pugi::xml_node root_apps = root.append_child (L"apps");
		pugi::xml_node root_rules_custom = root.append_child (L"rules_custom");
		pugi::xml_node root_rule_config = root.append_child (L"rules_config");

		_r_fastlock_acquireshared (&lock_access);

		// save apps
		if (root_apps)
		{
			for (auto &p : apps)
			{
				const PITEM_APP ptr_app = &p.second;

				if (!ptr_app->original_path)
					continue;

				const bool is_used = _app_isappused (ptr_app, p.first);

				if (!is_used && (ptr_app->type == DataAppService || ptr_app->type == DataAppUWP))
					continue;

				pugi::xml_node item = root_apps.append_child (L"item");

				if (item)
				{
					item.append_attribute (L"path").set_value (ptr_app->original_path);

					if (ptr_app->timestamp)
						item.append_attribute (L"timestamp").set_value (ptr_app->timestamp);

					// set timer (if presented)
					if (_app_istimeractive (ptr_app))
						item.append_attribute (L"timer").set_value (ptr_app->timer);

					// set last notification timestamp (if presented)
					if (ptr_app->last_notify)
						item.append_attribute (L"last_notify").set_value (ptr_app->last_notify);

					if (ptr_app->is_silent)
						item.append_attribute (L"is_silent").set_value (ptr_app->is_silent);

					if (ptr_app->is_enabled)
						item.append_attribute (L"is_enabled").set_value (ptr_app->is_enabled);
				}
			}
		}

		// save user rules
		if (root_rules_custom)
		{
			for (size_t i = 0; i < rules_arr.size (); i++)
			{
				const PITEM_RULE ptr_rule = rules_arr.at (i);

				if (!ptr_rule || ptr_rule->is_readonly || !ptr_rule->pname)
					continue;

				pugi::xml_node item = root_rules_custom.append_child (L"item");

				if (item)
				{
					item.append_attribute (L"name").set_value (ptr_rule->pname);

					if (ptr_rule->prule_remote && ptr_rule->prule_remote[0])
						item.append_attribute (L"rule").set_value (ptr_rule->prule_remote);

					if (ptr_rule->prule_local && ptr_rule->prule_local[0])
						item.append_attribute (L"rule_local").set_value (ptr_rule->prule_local);

					if (ptr_rule->dir != FWP_DIRECTION_OUTBOUND)
						item.append_attribute (L"dir").set_value (ptr_rule->dir);

					if (ptr_rule->protocol != 0)
						item.append_attribute (L"protocol").set_value (ptr_rule->protocol);

					if (ptr_rule->af != AF_UNSPEC)
						item.append_attribute (L"version").set_value (ptr_rule->af);

					// add apps attribute

					if (!ptr_rule->apps.empty ())
					{
						const rstring rule_apps = _app_rulesexpand (ptr_rule, false, RULE_DELIMETER);

						if (!rule_apps.IsEmpty ())
							item.append_attribute (L"apps").set_value (rule_apps);
					}

					if (ptr_rule->is_block)
						item.append_attribute (L"is_block").set_value (ptr_rule->is_block);

					if (ptr_rule->is_enabled)
						item.append_attribute (L"is_enabled").set_value (ptr_rule->is_enabled);
				}
			}
		}

		// save rules config
		if (root_rule_config)
		{
			for (auto const &p : rules_config)
			{
				const PITEM_RULE_CONFIG ptr_config = p.second;

				if (!ptr_config || !ptr_config->pname || !ptr_config->pname[0])
					continue;

				pugi::xml_node item = root_rule_config.append_child (L"item");

				if (item)
				{
					item.append_attribute (L"name").set_value (p.second->pname);

					// save apps
					const size_t rule_hash = _r_str_hash (ptr_config->pname);
					const PITEM_RULE ptr_rule = _app_getrule (rule_hash, DataRuleCustom, true);

					if (ptr_rule && !ptr_rule->apps.empty ())
					{
						const rstring rule_apps = _app_rulesexpand (ptr_rule, false, RULE_DELIMETER);

						if (!rule_apps.IsEmpty ())
							item.append_attribute (L"apps").set_value (rule_apps);
					}

					if (p.second->is_enabled)
						item.append_attribute (L"is_enabled").set_value (p.second->is_enabled);
				}
			}
		}

		doc.save_file (path_custom ? path_custom : config.profile_path, L"\t", PUGIXML_SAVE_FLAGS, PUGIXML_SAVE_ENCODING);

		_r_fastlock_releaseshared (&lock_access);

		// make backup
		if (is_backuprequired)
		{
			doc.save_file (config.profile_path_backup, L"\t", PUGIXML_SAVE_FLAGS, PUGIXML_SAVE_ENCODING);
			app.ConfigSet (L"BackupTimestamp", current_time);
		}
	}
}
