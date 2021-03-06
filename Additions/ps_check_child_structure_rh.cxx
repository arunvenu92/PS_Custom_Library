#include "ps_global.hxx"
#include "ps_defines.hxx"
#include "ps_rule_handlers.hxx"

using namespace ps;

int ps_check_child_structure_rh(EPM_rule_message_t msg)
{
	const char		*debug_name = "PS2-check-child-structure-RH";
	char			*pszArg = NULL;
	tag_t           tRootTask;
	c_ptr<tag_t>	tTargets;
	string			revRule;
	vector<string>	includeTargetTypes;
	vector<string>	includeChildTypes;
	string			bomViewType;
	logical			allowIfTarget = false;
	static tag_t	tRevRule = 0;
	vector<string>	targetItemIds;
	vector<tag_t>	targetsToProcess;
	static tag_t	tItemRevClassId = 0;
	tag_t			tBomWindow = 0;
	h_args			args(msg.arguments);
	EPM_decision_t  decision = EPM_go;

	log_debug("[START] %s", debug_name);
	hr_start_debug(debug_name);

	try
	{
		if (args.size() == 0)
			throw psexception("Missing mandatory arguments.");

		if (!args.getStr("rev_rule", revRule))
			throw psexception("Missing mandatory argument 'rev_rule'.");
		if (!args.getStr("bomview_type", bomViewType))
			throw psexception("Missing mandatory argument 'bomview_type'.");
		args.getVec("include_target_types", includeTargetTypes);
		args.getVec("include_child_types", includeChildTypes);
		args.getFlag("allow_if_target", allowIfTarget);

		if (tRevRule == 0)
		{
			itk(CFM_find(revRule.c_str(), &tRevRule));
			if (tRevRule == 0)
				throw psexception("Revision rule '" + revRule + "' could not be found.");
		}

		if (tItemRevClassId == 0)
		{
			itk(POM_class_id_of_class("ItemRevision", &tItemRevClassId));
		}

		itk(EPM_ask_root_task(msg.task, &tRootTask));
		itk(EPM_ask_attachments(tRootTask, EPM_target_attachment, tTargets.plen(), tTargets.pptr()));

		// Iterate over all targets to pre-populate targets to process and their item ids
		for (int i = 0; i < tTargets.len(); i++)
		{
			tag_t			tTarget = tTargets.val(i);
			tag_t			tClassOfTarget;
			tag_t			tItem;
			c_ptr<char>		itemId;
			c_ptr<char>		objectType;
			logical			isDescendant = false;

			itk(POM_class_of_instance(tTarget, &tClassOfTarget));
			itk(POM_is_descendant(tItemRevClassId, tClassOfTarget, &isDescendant));
			
			if (isDescendant)
			{
				itk(AOM_ask_value_string(tTarget, "object_type", objectType.pptr()));

				// Skip target if not of valid type
				if (!find_string(objectType.ptr(), includeTargetTypes))
					continue;

				itk(AOM_ask_value_tag(tTarget, "items_tag", &tItem));
				itk(AOM_ask_value_string(tItem, "item_id", itemId.pptr()));

				targetsToProcess.push_back(tTarget);
				targetItemIds.push_back(string(itemId.ptr()));
			}
		}

		// Loop over all target attachments
		for (vector<tag_t>::iterator i = targetsToProcess.begin(); i != targetsToProcess.end(); ++i)
		{
			tag_t			tTarget = *i;
			tag_t			tBvr = 0;
			tag_t			tTopLine;
			c_ptr<tag_t>	bvRevs;
			c_ptr<tag_t>	tChildren;

			itk(AOM_ask_value_tags(tTarget, "structure_revisions", bvRevs.plen(), bvRevs.pptr()));

			// Skip target if no bom view revisions
			if (bvRevs.len() == 0)
				continue;

			for (int j = 0; j < bvRevs.len(); j++)
			{
				tag_t			bvRev = bvRevs.val(j);
				c_ptr<char>		bvType;

				itk(AOM_ask_value_string(bvRev, "object_type", bvType.pptr()));

				if (bomViewType == bvType.ptr())
				{
					tBvr = bvRev;
					break;
				}
			}

			// No bvr of correct type found
			if (tBvr == 0)
				continue;

			itk(BOM_create_window(&tBomWindow));
			itk(BOM_set_window_config_rule(tBomWindow, tRevRule));
			itk(BOM_set_window_top_line_bvr(tBomWindow, tBvr, &tTopLine));
			itk(BOM_line_ask_child_lines(tTopLine, tChildren.plen(), tChildren.pptr()));

			// Loop over all structure children and verify the validity
			for (int j = 0; j < tChildren.len(); j++)
			{
				tag_t			tChild = tChildren.val(j);
				c_ptr<char>		lineConfigured;
				c_ptr<char>		lineItemId;
				c_ptr<char>		lineObjectType;
				c_ptr<char>		lineObjectStr;
				c_ptr<char>		targetObjectStr;

				itk(AOM_ask_value_string(tChild, "bl_rev_object_type", lineObjectType.pptr()));

				// If not valid structure type, jump to next target
				if (!find_string(lineObjectType.ptr(), includeChildTypes))
					continue;

				itk(AOM_ask_value_string(tChild, "bl_config_string", lineConfigured.pptr()));

				// If not configured bom line
				if (match_string(lineConfigured.ptr(), "No configured Revision"))
				{
					logical		allowed = false;
					
					if (allowIfTarget)
					{
						itk(AOM_ask_value_string(tChild, "bl_item_item_id", lineItemId.pptr()));

						if (find_string(lineItemId.ptr(), targetItemIds))
							allowed = true;
					}

					// If non valid child in structure
					if (!allowed)
					{
						decision = EPM_nogo;

						itk(AOM_ask_value_string(tChild, "bl_formatted_ancestor_name", lineObjectStr.pptr()));
						itk(AOM_ask_value_string(tTarget, "object_string", targetObjectStr.pptr()));

						itk(EMH_store_error_s1(EMH_severity_error, RULE_HANDLER_DEFAULT_IFAIL, c_ptr<char>("Structure child line '%s' of target revision '%s' is not valid for this process.", lineObjectStr.ptr(), targetObjectStr.ptr()).ptr()));
					}
				}
			}
			itk(BOM_close_window(tBomWindow));
		}
	}
	catch (tcexception& e)
	{
		if (tBomWindow != 0)
			BOM_close_window(tBomWindow);
		decision = EPM_nogo;
		EMH_store_error_s1(EMH_severity_error, RULE_HANDLER_DEFAULT_IFAIL, e.what());
		log_error(e.what());
	}
	catch (psexception& e)
	{
		if (tBomWindow != 0)
			BOM_close_window(tBomWindow);
		decision = EPM_nogo;
		EMH_store_error_s1(EMH_severity_error, RULE_HANDLER_DEFAULT_IFAIL, e.what());
		log_error(e.what());
	}

	hr_stop_debug(debug_name);
	hr_print_debug(debug_name);
	log_debug("[STOP] %s", debug_name);

	return decision;
}