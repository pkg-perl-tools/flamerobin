/*
  The contents of this file are subject to the Initial Developer's Public
  License Version 1.0 (the "License"); you may not use this file except in
  compliance with the License. You may obtain a copy of the License here:
  http://www.flamerobin.org/license.html.

  Software distributed under the License is distributed on an "AS IS"
  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
  License for the specific language governing rights and limitations under
  the License.

  The Original Code is FlameRobin (TM).

  The Initial Developer of the Original Code is Milan Babuskov.

  Portions created by the original developer
  are Copyright (C) 2004 Milan Babuskov.

  All Rights Reserved.

  Contributor(s): Nando Dessena
*/

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWindows headers
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

//
//
//
//
//------------------------------------------------------------------------------
#include <sstream>
#include "metadataitem.h"
#include "database.h"
#include "dberror.h"

YxMetadataItem::YxMetadataItem()
	: YxSubject()
{
	parentM = 0;
	typeM = ntUnknown;
	descriptionLoadedM = false;
}
//------------------------------------------------------------------------------
const std::string YxMetadataItem::getTypeName() const
{
	return "";
}
//------------------------------------------------------------------------------
const std::string YxMetadataItem::getItemPath() const
{
	std::string ret = getTypeName() + "(" + getPathName() + ")";
 	if (parentM)
 	{
		std::string parentItemPath = parentM->getItemPath();
		if (parentItemPath != "")
			ret = parentItemPath + "::" + ret;
	}
	return ret;
}
//------------------------------------------------------------------------------
const std::string YxMetadataItem::getPathName() const
{
	return getName();
}
//------------------------------------------------------------------------------
NodeType getTypeByName(std::string name)
{
	if (name == "TABLE")
		return ntTable;
	else if (name == "VIEW")
		return ntView;
	else if (name == "PROCEDURE")
		return ntProcedure;
	else if (name == "TRIGGER")
		return ntTrigger;
	else if (name == "GENERATOR")
		return ntGenerator;
	else if (name == "FUNCTION")
		return ntFunction;
	else if (name == "DOMAIN")
		return ntDomain;
	else if (name == "ROLE")
		return ntRole;
	else if (name == "COLUMN")
		return ntColumn;
    else if (name == "EXCEPTION")
        return ntException;
	else
		return ntUnknown;
}
//------------------------------------------------------------------------------
bool YxMetadataItem::getChildren(std::vector<YxMetadataItem*>& /*temp*/)
{
	return false;
}
//------------------------------------------------------------------------------
//! removes its children (by calling drop() for each) and notifies it's parent
void YxMetadataItem::drop()
{
	std::vector<YxMetadataItem *>temp;
	if (getChildren(temp))
		for (std::vector<YxMetadataItem *>::iterator it = temp.begin(); it != temp.end(); ++it)
			(*it)->drop();

	// TODO: prehaps the whole DBH needs to be reconsidered
	// we could write: if (parentM) parentM->remove(this);
	// but we can't, since parent might not be a collection!
	// ie. currently it is a YDatabase object
}
//------------------------------------------------------------------------------
YDatabase *YxMetadataItem::getDatabase() const
{
	if (getParent())
	{
		if (getParent()->getType() == ntDatabase)
			return dynamic_cast<YDatabase *>(getParent());
		else
			return getParent()->getDatabase();
	}
	else
		return 0;
}
//------------------------------------------------------------------------------
//! virtual so it can eventually be delegated to YTable, YView, etc.
std::string YxMetadataItem::getDescriptionSql() const
{
	switch (typeM)
	{
		case ntView:
		case ntTable:		return "select rdb$description from rdb$relations where RDB$RELATION_NAME=?";
		case ntProcedure:	return "select rdb$description from rdb$procedures where RDB$procedure_NAME=?";
		case ntTrigger:		return "select rdb$description from rdb$triggers where RDB$trigger_NAME=?";
		case ntFunction:	return "select rdb$description from RDB$FUNCTIONS where RDB$FUNCTION_NAME=?";
		case ntColumn:		return "select rdb$description from rdb$relation_fields where rdb$field_name=? and rdb$relation_name=?";
		case ntParameter:	return "select rdb$description from rdb$procedure_parameters where rdb$parameter_name=? and rdb$procedure_name=?";
		case ntDomain:		return "select rdb$description from rdb$fields where rdb$field_name=?";
        case ntException:   return "select RDB$DESCRIPTION from RDB$EXCEPTIONS where RDB$EXCEPTION_NAME = ?";
        default:			return "";
	};
}
//------------------------------------------------------------------------------
//! virtual so it can eventually be delegated to YTable, YView, etc.
std::string YxMetadataItem::getChangeDescriptionSql() const
{
	switch (typeM)
	{
		case ntView:
		case ntTable:		return "update rdb$relations set rdb$description = ? where RDB$RELATION_NAME = ?";
		case ntProcedure:	return "update rdb$procedures set rdb$description = ? where RDB$PROCEDURE_name=?";
		case ntTrigger:		return "update rdb$triggers set rdb$description = ? where RDB$trigger_NAME=?";
		case ntFunction:	return "update RDB$FUNCTIONS set rdb$description = ? where RDB$FUNCTION_NAME=?";
		case ntColumn:		return "update rdb$relation_fields set rdb$description = ? where rdb$field_name=? and rdb$relation_name=?";
		case ntParameter:	return "update rdb$procedure_parameters set rdb$description = ? where rdb$parameter_name = ? and rdb$procedure_name=?";
		case ntDomain:		return "update rdb$fields set rdb$description = ? where rdb$field_name=?";
        case ntException:   return "update RDB$EXCEPTIONS set RDB$DESCRIPTION = ? where RDB$EXCEPTION_NAME = ?";
		default:			return "";
	};
}
//------------------------------------------------------------------------------
//! ofObject = true   => returns list of objects this object depends on
//! ofObject = false  => returns list of objects that depend on this object
bool YxMetadataItem::getDependencies(std::vector<Dependency>& list, bool ofObject)
{
	YDatabase *d = getDatabase();
	if (!d)
	{
		lastError().setMessage("Database not set");
		return false;
	}

	int mytype = -1;			// map DBH type to RDB$DEPENDENT TYPE
	NodeType dep_types[] = { 	ntTable, 	ntView, 	ntTrigger, 	ntUnknown,	ntUnknown,
								ntProcedure,ntUnknown, 	ntException,ntUnknown,	ntUnknown,
								ntUnknown,	ntUnknown,	ntUnknown,	ntUnknown,  ntGenerator,
								ntFunction
	};
	int type_count = sizeof(dep_types)/sizeof(NodeType);
	for (int i=0; i<type_count; i++)
		if (typeM == dep_types[i])
			mytype = i;
	if (typeM == ntUnknown || mytype == -1)
	{
		lastError().setMessage("Unsupported type");
		return false;
	}

	try
	{
		IBPP::Database& db = d->getDatabase();
		IBPP::Transaction tr1 = IBPP::TransactionFactory(db, IBPP::amRead);
		tr1->Start();
		IBPP::Statement st1 = IBPP::StatementFactory(db, tr1);

		std::string o1 = (ofObject ? "DEPENDENT" : "DEPENDED_ON");
		std::string o2 = (ofObject ? "DEPENDED_ON" : "DEPENDENT");
		std::string sql =
			"select RDB$" + o2 + "_TYPE, RDB$" + o2 + "_NAME, RDB$FIELD_NAME "
			" from RDB$DEPENDENCIES "
			" where RDB$" + o1 + "_TYPE = ? and RDB$" + o1 + "_NAME = ? "
			" order by 1, 2, 3";
		st1->Prepare(sql);
		st1->Set(1, mytype);
		st1->Set(2, nameM);
		st1->Execute();
		YxMetadataItem *last = 0;
		Dependency *dep = 0;
		while (st1->Fetch())
		{
			std::string object_name, field_name;
			int object_type;
			st1->Get(1, &object_type);
			st1->Get(2, object_name);
			if (!st1->IsNull(3))
				st1->Get(3, field_name);
			object_name.erase(object_name.find_last_not_of(" ")+1);		// trim
			field_name.erase(field_name.find_last_not_of(" ")+1);		// trim

			if (object_type > type_count)	// some system object, not interesting for us
				continue;
			NodeType t = dep_types[object_type];
			if (t == ntUnknown)				// ditto
				continue;
			YxMetadataItem *current = d->findByNameAndType(t, object_name);
			if (!current)
			{								// maybe it's a view masked as table
				if (t == ntTable)
					current = d->findByNameAndType(ntView, object_name);
				if (!current)
					continue;
			}
			if (current != last)			// new object
			{
				Dependency de(current);
				list.push_back(de);
				dep = &list.back();
				last = current;
			}
			if (!st1->IsNull(3))
				dep->addField(field_name);
		}

		// TODO: perhaps this could be moved to YTable?
		//       call YxMetadataItem::getDependencies() and then add this
		if (typeM == ntTable && ofObject)	// foreign keys of this table
		{
			YTable *t = dynamic_cast<YTable *>(this);
			std::vector<ForeignKey> *f = t->getForeignKeys();
			for (std::vector<ForeignKey>::const_iterator it = f->begin(); it != f->end(); ++it)
			{
				YxMetadataItem *table = d->findByNameAndType(ntTable, (*it).referencedTableM);
				if (!table)
				{
					lastError().setMessage("Table " + (*it).referencedTableM + " not found.");
					return false;
				}
				Dependency de(table);
				de.setFields((*it).getReferencedColumnList());
				list.push_back(de);
			}
		}

		// TODO: perhaps this could be moved to YTable?
		if (typeM == ntTable && !ofObject)	// foreign keys of other tables
		{
			st1->Prepare(
				"select r1.rdb$relation_name, i.rdb$field_name "
				" from rdb$relation_constraints r1 "
				" join rdb$ref_constraints c on r1.rdb$constraint_name = c.rdb$constraint_name "
				" join rdb$relation_constraints r2 on c.RDB$CONST_NAME_UQ = r2.rdb$constraint_name "
				" join rdb$index_segments i on r1.rdb$index_name=i.rdb$index_name "
				" where r2.rdb$relation_name=? "
				" and r1.rdb$constraint_type='FOREIGN KEY' "
			);
			st1->Set(1, nameM);
			st1->Execute();
			std::string lasttable;
			Dependency *dep = 0;
			while (st1->Fetch())
			{
				std::string table_name, field_name;
				st1->Get(1, table_name);
				st1->Get(2, field_name);
				table_name.erase(table_name.find_last_not_of(" ")+1);		// trim
				field_name.erase(field_name.find_last_not_of(" ")+1);		// trim
				if (table_name != lasttable)	// new
				{
					YxMetadataItem *table = d->findByNameAndType(ntTable, table_name);
					if (!table)
						continue;			// dummy check
					Dependency de(table);
					list.push_back(de);
					dep = &list.back();
					lasttable = table_name;
				}
				dep->addField(field_name);
			}
		}

		tr1->Commit();
		return true;
	}
	catch (IBPP::Exception &e)
	{
		lastError().setMessage(e.ErrorMessage());
	}
	catch (...)
	{
		lastError().setMessage("System error.");
	}
	return false;
}
//------------------------------------------------------------------------------
std::string YxMetadataItem::getDescription()
{
	if (descriptionLoadedM)
		return descriptionM;

	YDatabase *d = getDatabase();
	std::string sql = getDescriptionSql();
	if (!d || sql == "")
		return "N/A";

	descriptionM = "";
	try
	{
		IBPP::Database& db = d->getDatabase();
		IBPP::Transaction tr1 = IBPP::TransactionFactory(db, IBPP::amRead);
		tr1->Start();
		IBPP::Statement st1 = IBPP::StatementFactory(db, tr1);
		st1->Prepare(sql);
		st1->Set(1, nameM);
		if (typeM == ntColumn || typeM == ntParameter)
			st1->Set(2, getParent()->getName());	// table/view/SP name
		st1->Execute();
		st1->Fetch();
		descriptionLoadedM = true;
        if (st1->IsNull(1))
            return "";
		IBPP::Blob b = IBPP::BlobFactory(st1->Database(), st1->Transaction());
		st1->Get(1, b);
		b->Open();
		std::string desc;
		char buffer[8192];		// 8K block
        while (true)
		{
			int size = b->Read(buffer, 8192);
			if (size <= 0)
				break;
			buffer[size] = 0;
			descriptionM += buffer;
		}
		b->Close();
		tr1->Commit();
		descriptionLoadedM = true;
		return descriptionM;
	}
	catch (IBPP::Exception &e)
	{
		lastError().setMessage(e.ErrorMessage());
	}
	catch (...)
	{
		lastError().setMessage("System error.");
	}

	return lastError().getMessage();
}
//------------------------------------------------------------------------------
bool YxMetadataItem::setDescription(std::string description)
{
	YDatabase *d = getDatabase();
	if (!d)
		return false;

	std::string sql = getChangeDescriptionSql();
	if (sql == "")
	{
		lastError().setMessage("The object does not support descriptions");
		return false;
	}

	try
	{
		IBPP::Database& db = d->getDatabase();
		IBPP::Transaction tr1 = IBPP::TransactionFactory(db);
		tr1->Start();
		IBPP::Statement st1 = IBPP::StatementFactory(db, tr1);
		st1->Prepare(sql);

		if (!description.empty())
		{
            IBPP::Blob b = IBPP::BlobFactory(st1->Database(), st1->Transaction());
            b->Create();
    		b->Write(description.c_str(), description.length());
    		b->Close();
    		st1->Set(1, b);
        }
        else
            st1->SetNull(1);
		st1->Set(2, nameM);
		if (typeM == ntColumn || typeM == ntParameter)
			st1->Set(3, getParent()->getName());
		st1->Execute();
		tr1->Commit();
		descriptionLoadedM = true;
		descriptionM = description;
		notify();
		return true;
	}
	catch (IBPP::Exception &e)
	{
		lastError().setMessage(e.ErrorMessage());
	}
	catch (...)
	{
		lastError().setMessage("System error.");
	}
	return false;
}
//------------------------------------------------------------------------------
YxMetadataItem *YxMetadataItem::getParent() const
{
	return parentM;
}
//------------------------------------------------------------------------------
void YxMetadataItem::setParent(YxMetadataItem *parent)
{
	parentM = parent;
}
//------------------------------------------------------------------------------
std::string YxMetadataItem::getPrintableName() const
{
	size_t n = getChildrenCount();
 	if (n)
	{
		std::ostringstream ss;
		ss << nameM << " (" << n << ")";
		return ss.str();
	}
	else
		return nameM;
}
//------------------------------------------------------------------------------
const std::string& YxMetadataItem::getName() const
{
	return nameM;
}
//------------------------------------------------------------------------------
void YxMetadataItem::setName(std::string name)
{
	name.erase(name.find_last_not_of(' ')+1);		// right trim
	nameM = name;
	notify();
}
//------------------------------------------------------------------------------
NodeType YxMetadataItem::getType() const
{
	return typeM;
}
//------------------------------------------------------------------------------
void YxMetadataItem::setType(NodeType type)
{
	typeM = type;
}
//------------------------------------------------------------------------------
bool YxMetadataItem::isSystem() const
{
	return getName().substr(0, 4) == "RDB$";
}
//------------------------------------------------------------------------------
std::string YxMetadataItem::getDropSqlStatement() const
{
    return "DROP " + getTypeName() + " " + getName() + ";";
}
//------------------------------------------------------------------------------
