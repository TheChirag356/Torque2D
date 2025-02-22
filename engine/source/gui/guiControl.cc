//-----------------------------------------------------------------------------
// Copyright (c) 2013 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include "console/consoleTypes.h"
#include "console/console.h"
#include "console/consoleInternal.h"
#include "console/codeBlock.h"
#include "platform/event.h"
#include "graphics/gBitmap.h"
#include "graphics/dgl.h"
#include "input/actionMap.h"
#include "gui/guiCanvas.h"
#include "gui/guiControl.h"
#include "gui/guiDefaultControlRender.h"
#include "gui/editor/guiEditCtrl.h"
#include "string/unicode.h"
#include "collection/vector.h"
#include "2d/core/Utility.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <string>

#include "guiControl_ScriptBinding.h"

#ifndef _FRAMEALLOCATOR_H_
#include "memory/frameAllocator.h"
#endif

//------------------------------------------------------------------------------

IMPLEMENT_CONOBJECT_CHILDREN(GuiControl);

static EnumTable::Enums alignCtrlEnums[] =
{
   { AlignmentType::LeftAlign,          "left"      },
   { AlignmentType::CenterAlign,        "center"    },
   { AlignmentType::RightAlign,         "right"     },
   { AlignmentType::DefaultAlign,       "default"   }
};
static EnumTable gAlignCtrlTable(3, &alignCtrlEnums[0]);

static EnumTable::Enums vAlignCtrlEnums[] =
{
   { VertAlignmentType::TopVAlign,          "top"      },
   { VertAlignmentType::MiddleVAlign,        "middle"    },
   { VertAlignmentType::BottomVAlign,         "bottom"     },
   { VertAlignmentType::DefaultVAlign,       "default"   }
};
static EnumTable gVAlignCtrlTable(3, &vAlignCtrlEnums[0]);

//used to locate the next/prev responder when tab is pressed
S32 GuiControl::smCursorChanged           = -1;
GuiControl *GuiControl::smPrevResponder = NULL;
GuiControl *GuiControl::smCurResponder = NULL;

GuiEditCtrl *GuiControl::smEditorHandle = NULL;

bool GuiControl::smDesignTime = false;

GuiControl::GuiControl()
{
   mLayer = 0;
   mBounds.set(0, 0, 64, 64);
   mStoredExtent.set(0, 0);
   mRenderInsetLT.set(0, 0);
   mRenderInsetRB.set(0, 0);
   mMinExtent.set(0, 0);
   mStoredRelativePosH.set(0, 0);
   mStoredRelativePosV.set(0, 0);
   mUseRelPosH = false;
   mUseRelPosV = false;

   mProfile = NULL;

   mConsoleVariable     = StringTable->EmptyString;
   mConsoleCommand      = StringTable->EmptyString;
   mAltConsoleCommand   = StringTable->EmptyString;
   mAcceleratorKey      = StringTable->EmptyString;
   mLangTableName       = StringTable->EmptyString;
   mText                = StringTable->EmptyString;
   mTextID              = StringTable->EmptyString;

   mAlignment           = AlignmentType::DefaultAlign;
   mVAlignment          = VertAlignmentType::DefaultVAlign;
   mFontSizeAdjust      = 1;
   mFontColor.set(0, 0, 0, 255);
   mOverrideFontColor   = false;

   mLangTable           = NULL;
   mFirstResponder      = NULL;
   mCanSaveFieldDictionary = false;
   mVisible             = true;
   mActive              = false;
   mAwake               = false;
   mCanSave				= true;
   mHorizSizing         = horizResizeRight;
   mVertSizing          = vertResizeBottom;
   mTooltipProfile      = NULL;
   mTooltip             = StringTable->EmptyString;
   mTipHoverTime        = 1000;
   mTooltipWidth		= 250;
   mIsContainer         = false;
   mTextWrap			= false;
   mTextExtend          = false;
   mUseInput            = true;
}

GuiControl::~GuiControl()
{
}

bool GuiControl::onAdd()
{
   // Let Parent Do Work.
   if(!Parent::onAdd())
      return false;

   // Grab the classname of this object
   const char *cName = getClassName();

   // if we're a pure GuiControl, then we're a container by default.
   if(dStrcmp("GuiControl", cName) == 0)
      mIsContainer = true;

   // Clamp to minExtent
   mBounds.extent.x = getMax( mMinExtent.x, mBounds.extent.x );
   mBounds.extent.y = getMax( mMinExtent.y, mBounds.extent.y );


   // Add to root group.
   Sim::getGuiGroup()->addObject(this);

   // Return Success.
   return true;
}

void GuiControl::onChildAdded( GuiControl *child )
{
	if(mProfile)
	{
		//This will cause the child control to be centered if it needs to be.
		RectI innerRect = this->getInnerRect(mBounds.point, mBounds.extent, GuiControlState::NormalState, mProfile);
		child->parentResized(innerRect.extent, innerRect.extent);

		if (isMethod("onChildAdded"))
		{
			Con::executef(this, 3, "onChildAdded", child->getIdString());
		}
	}
}

void GuiControl::onChildRemoved(GuiControl* child)
{
	if (mProfile && isMethod("onChildRemoved"))
	{
		Con::executef(this, 3, "onChildRemoved", child->getIdString());
	}
}

static EnumTable::Enums horzEnums[] =
{
    { GuiControl::horizResizeRight,      "right"     },
    { GuiControl::horizResizeWidth,      "width"     },
    { GuiControl::horizResizeLeft,       "left"      },
   { GuiControl::horizResizeCenter,     "center"    },
   { GuiControl::horizResizeRelative,   "relative"  }
};
static EnumTable gHorizSizingTable(5, &horzEnums[0]);

static EnumTable::Enums vertEnums[] =
{
    { GuiControl::vertResizeBottom,      "bottom"     },
    { GuiControl::vertResizeHeight,      "height"     },
    { GuiControl::vertResizeTop,         "top"        },
   { GuiControl::vertResizeCenter,      "center"     },
   { GuiControl::vertResizeRelative,    "relative"   }
};
static EnumTable gVertSizingTable(5, &vertEnums[0]);

void GuiControl::initPersistFields()
{
   Parent::initPersistFields();

   // Things relevant only to the editor.
   addGroup("Gui Editing");
   addField("isContainer",       TypeBool,      Offset(mIsContainer, GuiControl));
   endGroup("Gui Editing");

   // Parent Group.
   addGroup("GuiControl");

   addField("Profile",           TypeGuiProfile,	Offset(mProfile, GuiControl));
   addField("HorizSizing",       TypeEnum,			Offset(mHorizSizing, GuiControl), 1, &gHorizSizingTable);
   addField("VertSizing",        TypeEnum,			Offset(mVertSizing, GuiControl), 1, &gVertSizingTable);

   addProtectedField("Position",          TypePoint2I,		Offset(mBounds.point, GuiControl), &setPositionFn, &defaultProtectedGetFn, "The location of the control in relation to its parent's content area.");
   addProtectedField("Extent",            TypePoint2I,		Offset(mBounds.extent, GuiControl), &setExtentFn, &defaultProtectedGetFn, "The size of the control writen as width and height.");
   addProtectedField("MinExtent",         TypePoint2I,		Offset(mMinExtent, GuiControl), &setMinExtentFn, &defaultProtectedGetFn, &writeMinExtentFn, "The extent will not shrink below this size.");
   addField("canSave",           TypeBool,			Offset(mCanSave, GuiControl));
   addField("Visible",           TypeBool,			Offset(mVisible, GuiControl));
   addField("useInput",          TypeBool,          Offset(mUseInput, GuiControl));
   addDepricatedField("Modal");
   addField("SetFirstResponder", TypeBool, Offset(mFirstResponder, GuiControl));

   addField("Variable",          TypeString,		Offset(mConsoleVariable, GuiControl));
   addField("Command",           TypeString,		Offset(mConsoleCommand, GuiControl));
   addField("AltCommand",        TypeString,		Offset(mAltConsoleCommand, GuiControl));
   addField("Accelerator",       TypeString,		Offset(mAcceleratorKey, GuiControl));
   addField("Active",			 TypeBool,			Offset(mActive, GuiControl));
   endGroup("GuiControl");	

   addGroup("ToolTip");
   addField("tooltipprofile",    TypeGuiProfile,	Offset(mTooltipProfile, GuiControl));
   addField("tooltip",           TypeString,		Offset(mTooltip, GuiControl));
   addField("tooltipWidth",      TypeS32,			Offset(mTooltipWidth, GuiControl));
   addField("hovertime",         TypeS32,			Offset(mTipHoverTime, GuiControl));
   endGroup("ToolTip");


   addGroup("Localization");
   addField("langTableMod",      TypeString,		Offset(mLangTableName, GuiControl));
   endGroup("Localization");

   addGroup("Text");
   addProtectedField("text", TypeCaseString, Offset(mText, GuiControl), setTextProperty, getTextProperty, "");
   addField("textID", TypeString, Offset(mTextID, GuiControl));
   addField("textWrap", TypeBool, Offset(mTextWrap, GuiControl), &writeTextWrapFn, "If true, text will wrap to additional lines.");
   addField("textExtend", TypeBool, Offset(mTextExtend, GuiControl), &writeTextExtendFn, "If true, extent will change based on the size of the control's text when possible.");
   addField("align", TypeEnum, Offset(mAlignment, GuiControl), 1, &gAlignCtrlTable);
   addField("vAlign", TypeEnum, Offset(mVAlignment, GuiControl), 1, &gVAlignCtrlTable);
   addField("fontSizeAdjust", TypeF32, Offset(mFontSizeAdjust, GuiControl), "A decimal value that is multiplied with the profile's fontSize to determine the control's actual font size.");
   addField("fontColor", TypeColorI, Offset(mFontColor, GuiControl), "A color to override the font color of the control's profile. OverrideFontColor must be set to true for this to work.");
   addField("overrideFontColor", TypeBool, Offset(mOverrideFontColor, GuiControl), "If true, the control's fontColor will override the profile's font color.");
   endGroup("Text");
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

LangTable * GuiControl::getGUILangTable()
{
    if(mLangTable)
        return mLangTable;

    if(mLangTableName && *mLangTableName)
    {
        mLangTable = (LangTable *)getModLangTable((const UTF8*)mLangTableName);
        return mLangTable;
    }

    GuiControl *parent = getParent();
    if(parent)
        return parent->getGUILangTable();

    return NULL;
}

const UTF8 * GuiControl::getGUIString(S32 id)
{
    LangTable *lt = getGUILangTable();
    if(lt)
        return lt->getString(id);

    return NULL;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //


void GuiControl::addObject(SimObject *object)
{
   GuiControl *ctrl = dynamic_cast<GuiControl *>(object);
   if(!ctrl)
   {
      AssertWarn(0, "GuiControl::addObject: attempted to add NON GuiControl to set");
      return;
   }

   if(object->getGroup() == this)
      return;

    Parent::addObject(object);

   AssertFatal(!ctrl->isAwake(), "GuiControl::addObject: object is already awake before add");
   if(mAwake)
      ctrl->awaken();

  // If we are a child, notify our parent that we've been added
  GuiControl *parent = ctrl->getParent();
  if( parent )
     parent->onChildAdded( ctrl );
}

void GuiControl::removeObject(SimObject *object)
{
	GuiControl *ctrl = dynamic_cast<GuiControl *>(object);
	if (!ctrl)
	{
		AssertWarn(0, "GuiControl::removeObject: attempted to remove NON GuiControl from set");
		return;
	}
	GuiControl *parent = ctrl->getParent();

   AssertFatal(mAwake == static_cast<GuiControl*>(object)->isAwake(), "GuiControl::removeObject: child control wake state is bad");
   if (mAwake)
      static_cast<GuiControl*>(object)->sleep();
    Parent::removeObject(object);

	// If we are a child, notify our parent that we've been removed
	if (parent)
		parent->onChildRemoved(ctrl);
}

GuiControl *GuiControl::getParent()
{
    SimObject *obj = getGroup();
    if (GuiControl* gui = dynamic_cast<GuiControl*>(obj))
      return gui;
   return 0;
}

GuiCanvas *GuiControl::getRoot()
{
   GuiControl *root = NULL;
    GuiControl *parent = getParent();
   while (parent)
   {
      root = parent;
      parent = parent->getParent();
   }
   if (root)
      return dynamic_cast<GuiCanvas*>(root);
   else
      return NULL;
}

void GuiControl::inspectPreApply()
{
   if(smDesignTime && smEditorHandle)
      smEditorHandle->controlInspectPreApply(this);
   
   // The canvas never sleeps
   if(mAwake && dynamic_cast<GuiCanvas*>(this) == NULL )
   {
      onSleep(); // release all our resources.
      mAwake = true;
   }
}

void GuiControl::inspectPostApply()
{
   // Shhhhhhh, you don't want to wake the canvas!
   if(mAwake && dynamic_cast<GuiCanvas*>(this) == NULL )
   {
      mAwake = false;
      onWake();
   }
   
   if(smDesignTime && smEditorHandle)
      smEditorHandle->controlInspectPostApply(this);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

Point2I GuiControl::localToGlobalCoord(const Point2I &src)
{
   Point2I ret = src;
   ret += (mBounds.point + mRenderInsetLT);
   GuiControl *walk = getParent();
   while(walk)
   {
      ret += (walk->getPosition() + walk->mRenderInsetLT);
      walk = walk->getParent();
   }
   return ret;
}

Point2I GuiControl::globalToLocalCoord(const Point2I &src)
{
   Point2I ret = src;
   ret -= (mBounds.point + mRenderInsetLT);
   GuiControl *walk = getParent();
   while(walk)
   {
      ret -= (walk->getPosition() + walk->mRenderInsetLT);
      walk = walk->getParent();
   }
   return ret;
}

//----------------------------------------------------------------
void GuiControl::resize(const Point2I &newPosition, const Point2I &newExtent)
{
   Point2I actualNewExtent = Point2I(getMax(mMinExtent.x, newExtent.x),
      getMax(mMinExtent.y, newExtent.y));

	Point2I oldExtent = mBounds.extent;

   // only do the child control resizing stuff if you really need to.
   bool extentChanged = (actualNewExtent != oldExtent);

   if (extentChanged) {
      //call set update both before and after
      setUpdate();
      mBounds.set(newPosition, actualNewExtent);
      iterator i;
      for(i = begin(); i != end(); i++)
      {
         GuiControl *ctrl = static_cast<GuiControl *>(*i);
         ctrl->parentResized(oldExtent - (ctrl->mRenderInsetLT + ctrl->mRenderInsetRB), actualNewExtent - (ctrl->mRenderInsetLT + ctrl->mRenderInsetRB));
      }

      GuiControl *parent = getParent();
      if (parent)
         parent->childResized(this);
      setUpdate();

	  if (isMethod("onResize"))
	  {
		  Con::executef(this, 2, "onResize");
	  }
   }
   else {
      mBounds.point = newPosition;
   }
}
void GuiControl::setPosition( const Point2I &newPosition )
{
   resize( newPosition, mBounds.extent );
}

void GuiControl::setExtent( const Point2I &newExtent )
{
   resize( mBounds.point, newExtent );
}

void GuiControl::setBounds( const RectI &newBounds )
{
   resize( newBounds.point, newBounds.extent );
}

void GuiControl::setLeft( S32 newLeft )
{
   resize( Point2I( newLeft, mBounds.point.y), mBounds.extent );
}

void GuiControl::setTop( S32 newTop )
{
   resize( Point2I( mBounds.point.x, newTop ), mBounds.extent );
}

void GuiControl::setWidth( S32 newWidth )
{
   resize( mBounds.point, Point2I( newWidth, mBounds.extent.y ) );
}

void GuiControl::setHeight( S32 newHeight )
{
   resize( mBounds.point, Point2I( mBounds.extent.x, newHeight ) );
}

void GuiControl::childResized(GuiControl *child)
{
   // Default to do nothing. Do not call resize from here as it will create an infinite loop.

	if (isMethod("onChildResized"))
	{
		Con::executef(this, 3, "onChildResized", child->getIdString());
	}
}

void GuiControl::parentResized(const Point2I &oldParentExtent, const Point2I &newParentExtent)
{
   Point2I newPosition = getPosition();
   Point2I newExtent = getExtent();

    S32 deltaX = newParentExtent.x - oldParentExtent.x;
    S32 deltaY = newParentExtent.y - oldParentExtent.y;

	//In the case of centering, we want to make doubly sure we are using the inner rect.
	GuiControl* parent = getParent();
	Point2I parentInnerExt = Point2I(newParentExtent);
	if(mHorizSizing == horizResizeCenter || mVertSizing == vertResizeCenter)
	{
		//This is based on the "new" outer extent of the parent.
      Point2I origin = Point2I(0, 0);
		parentInnerExt = getInnerRect(origin, parent->mBounds.extent, NormalState, parent->mProfile).extent;
	}

    if (mHorizSizing == horizResizeCenter)
       newPosition.x = (parentInnerExt.x - mBounds.extent.x) >> 1;
    else if (mHorizSizing == horizResizeWidth)
        newExtent.x += deltaX;
    else if (mHorizSizing == horizResizeLeft)
      newPosition.x += deltaX;
    else if (mHorizSizing == horizResizeRelative && oldParentExtent.x != 0)
    {
        Point2F percent = relPosBatteryH(newPosition.x, newExtent.x, oldParentExtent.x);
        S32 newLeft = mRound(percent.x * newParentExtent.x);
        S32 newRight = mRound(percent.y * newParentExtent.x);

        newPosition.x = newLeft;
        newExtent.x = newRight - newLeft;
    }

    if (mVertSizing == vertResizeCenter)
       newPosition.y = (parentInnerExt.y - mBounds.extent.y) >> 1;
    else if (mVertSizing == vertResizeHeight)
        newExtent.y += deltaY;
    else if (mVertSizing == vertResizeTop)
      newPosition.y += deltaY;
    else if(mVertSizing == vertResizeRelative && oldParentExtent.y != 0)
    {
        Point2F percent = relPosBatteryV(newPosition.y, newExtent.y, oldParentExtent.y);
        S32 newTop = mRound(percent.x * newParentExtent.y);
        S32 newBottom = mRound(percent.y * newParentExtent.y);

        newPosition.y = newTop;
        newExtent.y = newBottom - newTop;
    }

   newExtent = extentBattery(newExtent);

   resize(newPosition, newExtent);
}

Point2I GuiControl::extentBattery(Point2I &newExtent)
{
	if (mMinExtent.x == 0 && mMinExtent.y == 0)
	{
		return newExtent;
	}

	Point2I result = Point2I(newExtent);
	if (newExtent.x < mBounds.extent.x && newExtent.x < mMinExtent.x)
	{
		mStoredExtent.x += mBounds.extent.x > mMinExtent.x ? (mMinExtent.x - newExtent.x) : (mBounds.extent.x - newExtent.x);
		result.x = mMinExtent.x;
	}
	else if (newExtent.x > mBounds.extent.x && mStoredExtent.x > 0)
	{
		S32 charge = getMin(newExtent.x - mBounds.extent.x, mStoredExtent.x);
		mStoredExtent.x -= charge;
		result.x = newExtent.x - charge;
	}

	if (newExtent.y < mBounds.extent.y && newExtent.y < mMinExtent.y)
	{
		mStoredExtent.y += mBounds.extent.y > mMinExtent.y ? (mMinExtent.y - newExtent.y) : (mBounds.extent.y - newExtent.y);
		result.y = mMinExtent.y;
	}
	else if (newExtent.y > mBounds.extent.y && mStoredExtent.y > 0)
	{
		S32 charge = getMin(newExtent.y - mBounds.extent.y, mStoredExtent.y);
		mStoredExtent.y -= charge;
		result.y = newExtent.y - charge;
	}
	return result;
}

Point2F GuiControl::relPosBatteryH(S32 pos, S32 ext, S32 parentExt)
{
    if (!mUseRelPosH)
    {
        relPosBattery(mStoredRelativePosH, pos, ext, parentExt);
        mUseRelPosH = true;
    }
    return mStoredRelativePosH;
}

Point2F GuiControl::relPosBatteryV(S32 pos, S32 ext, S32 parentExt)
{
    if (!mUseRelPosV)
    {
        relPosBattery(mStoredRelativePosV, pos, ext, parentExt);
        mUseRelPosV = true;
    }
    return mStoredRelativePosV;
}

void GuiControl::relPosBattery(Point2F& battery, S32 pos, S32 ext, S32 parentExt)
{
    battery.x = static_cast<F32>(pos) / parentExt;
    battery.y = static_cast<F32>(pos + ext) / parentExt;
}

//----------------------------------------------------------------

void GuiControl::onRender(Point2I offset, const RectI &updateRect)
{
    RectI ctrlRect = applyMargins(offset, mBounds.extent, NormalState, mProfile);

	if (!ctrlRect.isValidRect())
	{
		return;
	}

	renderUniversalRect(ctrlRect, mProfile, NormalState);

	//Render Text
	dglSetBitmapModulation(getFontColor(mProfile));
	RectI fillRect = applyBorders(ctrlRect.point, ctrlRect.extent, NormalState, mProfile);
	RectI contentRect = applyPadding(fillRect.point, fillRect.extent, NormalState, mProfile);

	if(contentRect.isValidRect())
	{
		renderText(contentRect.point, contentRect.extent, mText, mProfile);

		//Render the childen
		renderChildControls(offset, contentRect, updateRect);
	}
}

RectI GuiControl::applyMargins(Point2I &offset, Point2I &extent, GuiControlState currentState, GuiControlProfile *profile)
{
	//Get the border profiles
	GuiBorderProfile *leftProfile = profile->getLeftBorder();
	GuiBorderProfile *rightProfile = profile->getRightBorder();
	GuiBorderProfile *topProfile = profile->getTopBorder();
	GuiBorderProfile *bottomProfile = profile->getBottomBorder();

	S32 leftSize = (leftProfile) ? leftProfile->getMargin(currentState) : 0;
	S32 rightSize = (rightProfile) ? rightProfile->getMargin(currentState) : 0;
	S32 topSize = (topProfile) ? topProfile->getMargin(currentState) : 0;
	S32 bottomSize = (bottomProfile) ? bottomProfile->getMargin(currentState) : 0;

	return RectI(offset.x + leftSize, offset.y + topSize, (extent.x - leftSize) - rightSize, (extent.y - topSize) - bottomSize);
}

RectI GuiControl::applyBorders(Point2I &offset, Point2I &extent, GuiControlState currentState, GuiControlProfile *profile)
{
	//Get the border profiles
	GuiBorderProfile *leftProfile = profile->getLeftBorder();
	GuiBorderProfile *rightProfile = profile->getRightBorder();
	GuiBorderProfile *topProfile = profile->getTopBorder();
	GuiBorderProfile *bottomProfile = profile->getBottomBorder();

	S32 leftSize = (leftProfile) ? leftProfile->getBorder(currentState) : 0;
	S32 rightSize = (rightProfile) ? rightProfile->getBorder(currentState) : 0;
	S32 topSize = (topProfile) ? topProfile->getBorder(currentState) : 0;
	S32 bottomSize = (bottomProfile) ? bottomProfile->getBorder(currentState) : 0;

	return RectI(offset.x + leftSize, offset.y + topSize, (extent.x - leftSize) - rightSize, (extent.y - topSize) - bottomSize);
}

RectI GuiControl::applyPadding(Point2I &offset, Point2I &extent, GuiControlState currentState, GuiControlProfile *profile)
{
	//Get the border profiles
	GuiBorderProfile *leftProfile = profile->getLeftBorder();
	GuiBorderProfile *rightProfile = profile->getRightBorder();
	GuiBorderProfile *topProfile = profile->getTopBorder();
	GuiBorderProfile *bottomProfile = profile->getBottomBorder();

	S32 leftSize = (leftProfile) ? leftProfile->getPadding(currentState) : 0;
	S32 rightSize = (rightProfile) ? rightProfile->getPadding(currentState) : 0;
	S32 topSize = (topProfile) ? topProfile->getPadding(currentState) : 0;
	S32 bottomSize = (bottomProfile) ? bottomProfile->getPadding(currentState) : 0;

	return RectI(offset.x + leftSize, offset.y + topSize, (extent.x - leftSize) - rightSize, (extent.y - topSize) - bottomSize);
}

RectI GuiControl::getInnerRect(Point2I &offset, Point2I &extent, GuiControlState currentState, GuiControlProfile *profile)
{
	//Get the border profiles
	GuiBorderProfile *leftProfile = profile->getLeftBorder();
	GuiBorderProfile *rightProfile = profile->getRightBorder();
	GuiBorderProfile *topProfile = profile->getTopBorder();
	GuiBorderProfile *bottomProfile = profile->getBottomBorder();

	S32 leftSize = (leftProfile) ? leftProfile->getMargin(currentState) + leftProfile->getBorder(currentState) + leftProfile->getPadding(currentState) : 0;
	S32 rightSize = (rightProfile) ? rightProfile->getMargin(currentState) + rightProfile->getBorder(currentState) + rightProfile->getPadding(currentState) : 0;
	S32 topSize = (topProfile) ? topProfile->getMargin(currentState) + topProfile->getBorder(currentState) + topProfile->getPadding(currentState) : 0;
	S32 bottomSize = (bottomProfile) ? bottomProfile->getMargin(currentState) + bottomProfile->getBorder(currentState) + bottomProfile->getPadding(currentState) : 0;

	return RectI(offset.x + leftSize, offset.y + topSize, (extent.x - leftSize) - rightSize, (extent.y - topSize) - bottomSize);
}

Point2I GuiControl::getOuterExtent(Point2I &innerExtent, GuiControlState currentState, GuiControlProfile *profile)
{
    return Point2I(getOuterWidth(innerExtent.x, currentState, profile), getOuterHeight(innerExtent.y, currentState, profile));
}

S32 GuiControl::getOuterWidth(S32 innerWidth, GuiControlState currentState, GuiControlProfile* profile)
{
    //Get the border profiles
    GuiBorderProfile* leftProfile = profile->getLeftBorder();
    GuiBorderProfile* rightProfile = profile->getRightBorder();

    S32 leftSize = (leftProfile) ? leftProfile->getMargin(currentState) + leftProfile->getBorder(currentState) + leftProfile->getPadding(currentState) : 0;
    S32 rightSize = (rightProfile) ? rightProfile->getMargin(currentState) + rightProfile->getBorder(currentState) + rightProfile->getPadding(currentState) : 0;

    return innerWidth + leftSize + rightSize;
}

S32 GuiControl::getOuterHeight(S32 innerHeight, GuiControlState currentState, GuiControlProfile* profile)
{
    //Get the border profiles
    GuiBorderProfile* topProfile = profile->getTopBorder();
    GuiBorderProfile* bottomProfile = profile->getBottomBorder();

    S32 topSize = (topProfile) ? topProfile->getMargin(currentState) + topProfile->getBorder(currentState) + topProfile->getPadding(currentState) : 0;
    S32 bottomSize = (bottomProfile) ? bottomProfile->getMargin(currentState) + bottomProfile->getBorder(currentState) + bottomProfile->getPadding(currentState) : 0;

    return innerHeight + topSize + bottomSize;
}

bool GuiControl::renderTooltip(Point2I &cursorPos, const char* tipText )
{
#if !defined(TORQUE_OS_IOS) && !defined(TORQUE_OS_ANDROID) && !defined(TORQUE_OS_EMSCRIPTEN)
    // Short Circuit.
    if (!mAwake) 
        return false;

    if ( dStrlen( mTooltip ) == 0 && ( tipText == NULL || dStrlen( tipText ) == 0 ) )
        return false;

    const char* renderTip = mTooltip;
    if( tipText != NULL )
        renderTip = tipText;

    // Finish if no root.
    GuiCanvas *root = getRoot();
    if ( !root )
        return false;

    if (!mTooltipProfile)
		setField("TooltipProfile", "GuiTooltipProfile");

    GFont *font = mTooltipProfile->getFont();
   
    // Set text bounds.
    Point2I textBounds( 0, 0 );

    // Fetch the width of a space.
    const S32 spaceWidth = (S32)font->getStrWidth(" ");

    // Fetch the maximum allowed tooltip extent.
    const S32 maxTooltipWidth = mTooltipWidth;

    // Fetch word count.
    const S32 wordCount = StringUnit::getUnitCount( renderTip, " " );

    // Reset line storage.
    const S32 tooltipLineStride = (S32)font->getHeight() + 4;
    const S32 maxTooltipLines = 20;
    S32 tooltipLineCount = 0;
    S32 tooltipLineWidth = 0;
    FrameTemp<StringBuffer> tooltipLines( maxTooltipLines );

    // Reset word indexing.
    S32 wordStartIndex = 0;
    S32 wordEndIndex = 0;

    // Search for end word.
    while( true )
    {
        // Do we have any words left?
        if ( wordEndIndex < wordCount )
        {
            // Yes, so fetch the word.
            const char* pWord = StringUnit::getUnit( renderTip, wordEndIndex, " " );

            // Add word length.
            const S32 wordLength = (S32)font->getStrWidth( pWord ) + spaceWidth;

            // Do we still have room?
            if ( (tooltipLineWidth + wordLength) < maxTooltipWidth )
            {
                // Yes, so add word length.
                tooltipLineWidth += wordLength;

                // Next word.
                wordEndIndex++;

                continue;
            }

            // Do we have any lines left?
            if ( tooltipLineCount < maxTooltipLines )
            {
                // Yes, so insert line.
                tooltipLines[tooltipLineCount++] = StringUnit::getUnits( renderTip, wordStartIndex, wordEndIndex-1, " " );

                // Update horizontal text bounds.
                if ( tooltipLineWidth > textBounds.x )
                    textBounds.x = tooltipLineWidth;
            }

            // Set new line length.
            tooltipLineWidth = wordLength;

            // Set word start.
            wordStartIndex = wordEndIndex;

            // Next word.
            wordEndIndex++;

            continue;
        }

        // Do we have any words left?
        if ( wordStartIndex < wordCount )
        {
            // Yes, so do we have any lines left?
            if ( tooltipLineCount < maxTooltipLines )
            {
                // Yes, so insert line.
                tooltipLines[tooltipLineCount++] = StringUnit::getUnits( renderTip, wordStartIndex, wordCount-1, " " );

                // Update horizontal text bounds.
                if ( tooltipLineWidth > textBounds.x )
                    textBounds.x = tooltipLineWidth;
            }
        }

        break;
    }

    // Controls the size of the inside (gutter) tooltip region.
    const S32 tooltipGutterSize = 5;

    // Adjust text bounds.
    textBounds.x += tooltipGutterSize * 2;
    textBounds.y = (((S32)font->getHeight() + 4) * tooltipLineCount - 4) + (tooltipGutterSize * 2);

    // Adjust to tooltip is always on-screen.
    Point2I screensize = Platform::getWindowSize();
    Point2I offset = cursorPos; 
    offset.y += 22;
    if (screensize.x < (offset.x + textBounds.x))
        offset.x = screensize.x - textBounds.x;
    if(screensize.y < (offset.y + textBounds.y) )
        offset.y = screensize.y - textBounds.y;

    // Fetch the old clip.
    RectI oldClip = dglGetClipRect();

    // Set rectangle for the box, and set the clip rectangle.
    RectI rect(offset, textBounds);
    dglSetClipRect(rect);

    // Draw body and border of the tool tip
	renderUniversalRect(rect, mTooltipProfile, NormalState);

    // Draw the text centered in the tool tip box
    dglSetBitmapModulation( mTooltipProfile->mFontColor );
    Point2I start( tooltipGutterSize, tooltipGutterSize );
    for ( S32 lineIndex = 0; lineIndex < tooltipLineCount; lineIndex++ )
    {
        dglDrawText( font, start + offset, tooltipLines[lineIndex].getPtr8(), mProfile->mFontColors );
        offset.y += tooltipLineStride;
    }

    dglSetClipRect( oldClip );
#endif
    return true;
}

void GuiControl::renderChildControls(Point2I offset, RectI content, const RectI &updateRect)
{
   // offset is the upper-left corner of this control in screen coordinates. It should almost always be the same offset passed into the onRender method.
   // updateRect is the area that this control was allowed to draw in. It should almost always be the same as the value in onRender.
   // content is the area that child controls are allowed to draw in.
   RectI clipRect = content;
   if(clipRect.intersect(dglGetClipRect()))
   {
	   S32 size = objectList.size();
	   S32 size_cpy = size;
		//-Mat look through our vector all normal-like, trying to use an iterator sometimes gives us
	   //bad cast on good objects
	   for( S32 count = 0; count < objectList.size(); count++ )
	   {
		  GuiControl *ctrl = (GuiControl *)objectList[count];
		  if( ctrl == NULL ) {
			  Con::errorf( "GuiControl::renderChildControls() object %i is NULL", count );
			continue;
		  }
		  if (ctrl->mVisible)
		  {
			 ctrl->mRenderInsetLT = content.point - offset;
			 ctrl->mRenderInsetRB = mBounds.extent - (ctrl->mRenderInsetLT + content.extent);
			 Point2I childPosition = content.point + ctrl->getPosition();
			 RectI childClip(childPosition, ctrl->getExtent());

			 if (childClip.intersect(clipRect))
			 {
				RectI old = dglGetClipRect();
				dglSetClipRect(clipRect);
				glDisable(GL_CULL_FACE);
				ctrl->onRender(childPosition, RectI(childPosition, ctrl->getExtent()));
				dglSetClipRect(old);
			 }
		  }
		  size_cpy = objectList.size(); //	CHRIS: i know its wierd but the size of the list changes sometimes during execution of this loop
		  if(size != size_cpy)
		  {
			  size = size_cpy;
			  count--;	//	CHRIS: just to make sure one wasnt skipped.
		  }
	   }
   }
}

void GuiControl::setUpdateRegion(Point2I pos, Point2I ext)
{
   Point2I upos = localToGlobalCoord(pos);
   GuiCanvas *root = getRoot();
   if (root)
   {
      root->addUpdateRegion(upos, ext);
   }
}

void GuiControl::setUpdate()
{
   setUpdateRegion(Point2I(0,0), mBounds.extent);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

void GuiControl::awaken()
{
   AssertFatal(!mAwake, "GuiControl::awaken: control is already awake");
   if(mAwake)
      return;

   iterator i;
   for(i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);

      AssertFatal(!ctrl->isAwake(), "GuiControl::awaken: child control is already awake");
      if(!ctrl->isAwake())
         ctrl->awaken();
   }

   AssertFatal(!mAwake, "GuiControl::awaken: should not be awake here");
   if(!mAwake)
   {
      if(!onWake())
      {
         Con::errorf(ConsoleLogEntry::General, "GuiControl::awaken: failed onWake for obj: %s", getName());
         AssertFatal(0, "GuiControl::awaken: failed onWake");
         deleteObject();
      }
	  else
	  {
		  if (mTextID && *mTextID != 0)
			  setTextID(mTextID);
	  }
   }
}

void GuiControl::sleep()
{
   AssertFatal(mAwake, "GuiControl::sleep: control is not awake");
   if(!mAwake)
      return;

   iterator i;
   for(i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);

      AssertFatal(ctrl->isAwake(), "GuiControl::sleep: child control is already asleep");
      if(ctrl->isAwake())
         ctrl->sleep();
   }

   AssertFatal(mAwake, "GuiControl::sleep: should not be asleep here");
   if(mAwake)
      onSleep();
}

void GuiControl::preRender()
{
   AssertFatal(mAwake, "GuiControl::preRender: control is not awake");
   if(!mAwake)
      return;

   iterator i;
   for(i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      if (ctrl->isVisible())
      {
          ctrl->preRender();
      }
   }
   onPreRender();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

bool GuiControl::onWake()
{
   AssertFatal( !mAwake, "GuiControl::onWake: control is already awake" );
   if( mAwake )
      return false;

   // [tom, 4/18/2005] Cause mLangTable to be refreshed in case it was changed
   mLangTable = NULL;

   // Grab the classname of this object
   const char *cName = getClassName();

   //make sure we have a profile
   if( !mProfile )
   {
      // Ensure the classname is a valid name...
      if( cName && cName[0] )
      {
         S32 pos = 0;

         for( pos = 0; pos <= (S32)dStrlen( cName ); pos++ )
            if( !dStrncmp( cName + pos, "Ctrl", 4 ) )
               break;

         if( pos != 0 ) {
            char buff[255];
            dStrncpy( buff, cName, pos );
            buff[pos] = '\0';
            dStrcat( buff, "Profile\0" );

            SimObject *obj = Sim::findObject( buff );

            if( obj )
               mProfile = dynamic_cast<GuiControlProfile*>( obj );
         }
      }

      // Ok lets check to see if that worked
      if( !mProfile ) {
         SimObject *obj = Sim::findObject( "GuiDefaultProfile" );

         if( obj )
            mProfile = dynamic_cast<GuiControlProfile*>(obj);
      }

      AssertFatal( mProfile, avar( "GuiControl: %s created with no profile.", getName() ) );
   }

   //set the flag
   mAwake = true;

   //set the layer
   GuiCanvas *root = getRoot();
   AssertFatal(root, "Unable to get the root Canvas.");
   GuiControl *parent = getParent();
   if (parent && parent != root)
      mLayer = parent->mLayer;

   //make sure the first responder exists
   if (! mFirstResponder)
      mFirstResponder = findFirstTabable();

   //see if we should force this control to be the first responder
   //if (mProfile->mTabable && mProfile->mCanKeyFocus)
   //   setFirstResponder();

   //increment the profile
   mProfile->incRefCount();

   // Only invoke script callbacks if we have a namespace in which to do so
   // This will suppress warnings
   if( isMethod("onWake") )
      Con::executef(this, 1, "onWake");

   if (mTooltipProfile != NULL)
	   mTooltipProfile->incRefCount();

   return true;
}

void GuiControl::onSleep()
{
   AssertFatal(mAwake, "GuiControl::onSleep: control is not awake");
   if(!mAwake)
      return;

   //decrement the profile referrence
   if( mProfile != NULL )
      mProfile->decRefCount();
   clearFirstResponder();
   mouseUnlock();

   // Only invoke script callbacks if we have a namespace in which to do so
   // This will suppress warnings
   if( isMethod("onSleep") )
      Con::executef(this, 1, "onSleep");

   if (mTooltipProfile != NULL)
	   mTooltipProfile->decRefCount();

   // Set Flag
   mAwake = false;
}

void GuiControl::setControlProfile(GuiControlProfile *prof)
{
   AssertFatal(prof, "GuiControl::setControlProfile: invalid profile");
   if(prof == mProfile)
      return;
   if(mAwake)
      mProfile->decRefCount();
   mProfile = prof;
   if(mAwake)
      mProfile->incRefCount();
}

void GuiControl::onPreRender()
{
   // do nothing.
}
//-----------------------------------------------------------------------------
//	checks up the parent hierarchy - if anyone above us is not savable returns false
//	otherwise, returns true.
//-----------------------------------------------------------------------------
bool	GuiControl::getCanSaveParent()
{
   GuiControl *walk = this;
   while(walk)
   {
      if(!walk->getCanSave())
         return false;

      walk = walk->getParent();
   }

   return true;
}

//-----------------------------------------------------------------------------
//	Can we Save to a TorqueScript file?
//-----------------------------------------------------------------------------
bool GuiControl::getCanSave()
{
   return mCanSave;
}

//-----------------------------------------------------------------------------
//	Sets whether we can save out to a file (TorqueScript)
//-----------------------------------------------------------------------------
void GuiControl::setCanSave(bool bCanSave)
{
   mCanSave	=	bCanSave;
}



////////////////////////////////////////////////////////////////////////////////////////////////////
//	checks out mCanSave flag, if true just passes along to our parent, 
//	if false, then we return without writing.  Note, also, that 
//	if our parent is not writeable, then we should not be writable...
////////////////////////////////////////////////////////////////////////////////////////////////////
void GuiControl::write(Stream &stream, U32 tabStop, U32 flags)
{
   //note: this will return false if either we, or any of our parents, are non-save controls
   bool bCanSave	=	getCanSaveParent();
   if(bCanSave)
   {
      Parent::write(stream, tabStop, flags);
   }

}




//This is only called if the control is deleted, not when the control is removed from its parent.
void GuiControl::onRemove()
{
	Parent::onRemove();
}

//For GuiControls, this will always just before it is actually removed.
void GuiControl::onGroupRemove()
{
	clearFirstResponder();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

const char *GuiControl::getScriptValue()
{
   return NULL;
}

void GuiControl::setScriptValue(const char *value)
{
}

void GuiControl::setConsoleVariable(const char *variable)
{
   if (variable)
   {
      mConsoleVariable = StringTable->insert(variable);
   }
   else
   {
      mConsoleVariable = StringTable->EmptyString;
   }
}
  
//-----------------------------------------------------------------------------
//	finds and returns the first immediate child of ours whose
//	internal name matches the passed String. returns Null if not found.
//-----------------------------------------------------------------------------
void GuiControl::setConsoleCommand(const char *newCmd)
{
   if (newCmd)
      mConsoleCommand = StringTable->insert(newCmd);
   else
      mConsoleCommand = StringTable->EmptyString;
}

const char * GuiControl::getConsoleCommand()
{
    return mConsoleCommand;
}

void GuiControl::setSizing(S32 horz, S32 vert)
{
    mHorizSizing = horz;
    mVertSizing = vert;
}


void GuiControl::setVariable(const char *value)
{
   if (mConsoleVariable[0])
      Con::setVariable(mConsoleVariable, value);
}

void GuiControl::setIntVariable(S32 value)
{
   if (mConsoleVariable[0])
      Con::setIntVariable(mConsoleVariable, value);
}

void GuiControl::setFloatVariable(F32 value)
{
   if (mConsoleVariable[0])
      Con::setFloatVariable(mConsoleVariable, value);
}

const char * GuiControl::getVariable()
{
   if (mConsoleVariable[0])
      return Con::getVariable(mConsoleVariable);
   else return NULL;
}

S32 GuiControl::getIntVariable()
{
   if (mConsoleVariable[0])
      return Con::getIntVariable(mConsoleVariable);
   else return 0;
}

F32 GuiControl::getFloatVariable()
{
   if (mConsoleVariable[0])
      return Con::getFloatVariable(mConsoleVariable);
   else return 0.0f;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

bool GuiControl::cursorInControl()
{
   GuiCanvas *root = getRoot();
   if (! root) return false;

   Point2I pt = root->getCursorPos();
   Point2I offset = localToGlobalCoord(Point2I(0, 0));
   if (pt.x >= offset.x && pt.y >= offset.y &&
      pt.x < offset.x + mBounds.extent.x && pt.y < offset.y + mBounds.extent.y)
   {
      return true;
   }
   else
   {
      return false;
   }
}

bool GuiControl::pointInControl(const Point2I& parentCoordPoint)
{
   S32 xt = parentCoordPoint.x - mBounds.point.x;
   S32 yt = parentCoordPoint.y - mBounds.point.y;
   return xt >= 0 && yt >= 0 && xt < mBounds.extent.x && yt < mBounds.extent.y;
}


GuiControl* GuiControl::findHitControl(const Point2I &pt, S32 initialLayer)
{
   iterator i = end(); // find in z order (last to first)
   while (i != begin())
   {
      i--;
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      if (initialLayer >= 0 && ctrl->mLayer > initialLayer)
      {
         continue;
      }
      else if (ctrl->mVisible && ctrl->pointInControl(pt - ctrl->mRenderInsetLT) && ctrl->mUseInput)
      {
         Point2I ptemp = pt - (ctrl->mBounds.point + ctrl->mRenderInsetLT);
         GuiControl *hitCtrl = ctrl->findHitControl(ptemp);

         if(hitCtrl->mUseInput)
            return hitCtrl;
      }
   }
   return this;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

bool GuiControl::isMouseLocked()
{
   GuiCanvas *root = getRoot();
   return root ? root->getMouseLockedControl() == this : false;
}

void GuiControl::mouseLock(GuiControl *lockingControl)
{
   GuiCanvas *root = getRoot();
   if (root)
      root->mouseLock(lockingControl);
}

void GuiControl::mouseLock()
{
   GuiCanvas *root = getRoot();
   if (root)
      root->mouseLock(this);
}

void GuiControl::mouseUnlock()
{
   GuiCanvas *root = getRoot();
   if (root)
      root->mouseUnlock(this);
}

bool GuiControl::sendScriptMouseEvent(const char* name, const GuiEvent& event)
{
    bool consumed = false;
    if (isMethod(name))
    {
        char buf[3][32];
        dSprintf(buf[0], 32, "%d", event.modifier);
        dSprintf(buf[1], 32, "%d %d", event.mousePoint.x, event.mousePoint.y);
        dSprintf(buf[2], 32, "%d", event.mouseClickCount);
        consumed = dAtob(Con::executef(this, 4, name, buf[0], buf[1], buf[2]));
    }
    return consumed;
}

bool GuiControl::sendScriptKeyEvent(const char* name, const InputEvent& event)
{
    bool consumed = false;
    if (isMethod(name))
    {
        char buf[2][32];
        dSprintf(buf[0], 32, "%d", event.modifier);
        if (!ActionMap::getKeyString(event.objInst, buf[1]))
            return(false);
        consumed = dAtob(Con::executef(this, 3, name, buf[0], buf[1]));
    }
    return consumed;
}

bool GuiControl::onInputEvent(const InputEvent &event)
{
    if (event.objType == SI_KEY)
    {
        if (event.action == SI_MAKE)
        {
            return sendScriptKeyEvent("onKeyDown", event);
        }
        else if (event.action == SI_BREAK)
        {
            return sendScriptKeyEvent("onKeyUp", event);
        }
        else if (event.action == SI_REPEAT)
        {
            return sendScriptKeyEvent("onKeyRepeat", event);
        }
    }

    return false;
}

void GuiControl::onTouchUp(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed1 = sendScriptMouseEvent("onTouchUp", event);
    bool consumed2 = sendScriptMouseEvent("onMouseUp", event);

    GuiControl* parent = getParent();
    if (parent && !consumed1 && !consumed2)
        parent->onTouchUp(event);
}

void GuiControl::onTouchDown(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed1 = sendScriptMouseEvent("onTouchDown", event);
    bool consumed2 = sendScriptMouseEvent("onMouseDown", event);

    GuiControl* parent = getParent();
    if (parent && !consumed1 && !consumed2)
        parent->onTouchDown(event);
}

void GuiControl::onTouchMove(const GuiEvent &event)
{
   if ( !mVisible || !mAwake )
      return;

   bool consumed1 = sendScriptMouseEvent("onTouchMove", event);
   bool consumed2 = sendScriptMouseEvent("onMouseMove", event);

   GuiControl *parent = getParent();
   if (parent && !consumed1 && !consumed2)
      parent->onTouchMove( event );
}

void GuiControl::onTouchDragged(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed1 = sendScriptMouseEvent("onTouchDragged", event);
    bool consumed2 = sendScriptMouseEvent("onMouseDragged", event);

    GuiControl* parent = getParent();
    if (parent && !consumed1 && !consumed2)
        parent->onTouchDragged(event);
}

void GuiControl::onTouchEnter(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    sendScriptMouseEvent("onTouchEnter", event);
    sendScriptMouseEvent("onMouseEnter", event);

    //Entering a child means nothing to a parent
}

void GuiControl::onTouchLeave(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    sendScriptMouseEvent("onTouchLeave", event);
    sendScriptMouseEvent("onMouseLeave", event);

    //Leaving a child means nothing to a parent
}

void GuiControl::onMouseWheelUp( const GuiEvent &event )
{
   if ( !mVisible || !mAwake )
      return;

   bool consumed = sendScriptMouseEvent("onMouseWheelUp", event);

   GuiControl *parent = getParent();
   if (parent && !consumed)
      return parent->onMouseWheelUp(event);
}

void GuiControl::onMouseWheelDown( const GuiEvent &event )
{
   if ( !mVisible || !mAwake )
      return;

   bool consumed = sendScriptMouseEvent("onMouseWheelDown", event);

   GuiControl *parent = getParent();
   if (parent && !consumed)
      return parent->onMouseWheelDown(event);
}

void GuiControl::onRightMouseDown(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onRightMouseDown", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onRightMouseDown(event);
}

void GuiControl::onRightMouseUp(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onRightMouseUp", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onRightMouseUp(event);
}

void GuiControl::onRightMouseDragged(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onRightMouseDragged", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onRightMouseDragged(event);
}

void GuiControl::onMiddleMouseDown(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onMiddleMouseDown", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onMiddleMouseDown(event);
}

void GuiControl::onMiddleMouseUp(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onMiddleMouseUp", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onMiddleMouseUp(event);
}

void GuiControl::onMiddleMouseDragged(const GuiEvent &event)
{
    if (!mVisible || !mAwake)
        return;

    bool consumed = sendScriptMouseEvent("onMiddleMouseDragged", event);

    GuiControl* parent = getParent();
    if (parent && !consumed)
        parent->onMiddleMouseDragged(event);
}


GuiControl* GuiControl::findFirstTabable()
{
   GuiControl *tabCtrl = NULL;
   iterator i;
   for (i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      tabCtrl = ctrl->findFirstTabable();
      if (tabCtrl)
      {
         mFirstResponder = tabCtrl;
         return tabCtrl;
      }
   }

   //nothing was found, therefore, see if this ctrl is tabable
   return ( mProfile != NULL ) ? ( ( mProfile->mTabable && mAwake && mVisible ) ? this : NULL ) : NULL;
}

GuiControl* GuiControl::findLastTabable(bool firstCall)
{
   //if this is the first call, clear the global
   if (firstCall)
      smPrevResponder = NULL;

   //if this control is tabable, set the global
   if (mProfile->mTabable)
      smPrevResponder = this;

   iterator i;
   for (i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      ctrl->findLastTabable(false);
   }

   //after the entire tree has been traversed, return the last responder found
   mFirstResponder = smPrevResponder;
   return smPrevResponder;
}

GuiControl *GuiControl::findNextTabable(GuiControl *curResponder, bool firstCall)
{
   //if this is the first call, clear the global
   if (firstCall)
      smCurResponder = NULL;

   //first find the current responder
   if (curResponder == this)
      smCurResponder = this;

   //if the first responder has been found, return the very next *tabable* control
   else if ( smCurResponder && mProfile->mTabable && mAwake && mVisible && mActive )
      return( this );

   //loop through, checking each child to see if it is the one that follows the firstResponder
   GuiControl *tabCtrl = NULL;
   iterator i;
   for (i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      tabCtrl = ctrl->findNextTabable(curResponder, false);
      if (tabCtrl) break;
   }
   mFirstResponder = tabCtrl;
   return tabCtrl;
}

GuiControl *GuiControl::findPrevTabable(GuiControl *curResponder, bool firstCall)
{
   if (firstCall)
      smPrevResponder = NULL;

   //if this is the current reponder, return the previous one
   if (curResponder == this)
      return smPrevResponder;

   //else if this is a responder, store it in case the next found is the current responder
   else if ( mProfile->mTabable && mAwake && mVisible && mActive )
      smPrevResponder = this;

   //loop through, checking each child to see if it is the one that follows the firstResponder
   GuiControl *tabCtrl = NULL;
   iterator i;
   for (i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      tabCtrl = ctrl->findPrevTabable(curResponder, false);
      if (tabCtrl) break;
   }
   mFirstResponder = tabCtrl;
   return tabCtrl;
}

void GuiControl::onLoseFirstResponder()
{
    // Since many controls have visual cues when they are the firstResponder...
    setUpdate();

	if (isMethod("onLoseFirstResponder"))
	{
		Con::executef(this, 2, "onLoseFirstResponder");
	}
	else if (isMethod("onBlur"))
	{
		Con::executef(this, 2, "onBlur");
	}
}

bool GuiControl::ControlIsChild(GuiControl *child)
{
   //function returns true if this control, or one of it's children is the child control
   if (child == this)
      return true;

   //loop through, checking each child to see if it is ,or contains, the firstResponder
   iterator i;
   for (i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      if (ctrl->ControlIsChild(child)) return true;
   }

   //not found, therefore false
   return false;
}

void GuiControl::onFocus()
{
	//bubble the focus up
	GuiControl *parent = getParent();
	if (parent)
		parent->onFocus();
}

bool GuiControl::isFirstResponder()
{
   GuiCanvas *root = getRoot();
   return root && root->getFirstResponder() == this;
}

void GuiControl::setFirstResponder( GuiControl* firstResponder )
{
   if ( firstResponder && firstResponder->mProfile->mCanKeyFocus )
      mFirstResponder = firstResponder;

   GuiControl *parent = getParent();
   if ( parent )
      parent->setFirstResponder( firstResponder );
}

void GuiControl::setFirstResponder()
{
    if ( mAwake && mVisible )
    {
		GuiControl *parent = getParent();
		if (mProfile->mCanKeyFocus == true && parent != NULL )
		{
			parent->setFirstResponder(this);

			// Since many controls have visual cues when they are the firstResponder...
			this->setUpdate();	

			if (isMethod("onGainFirstResponder"))
			{
				Con::executef(this, 2, "onGainFirstResponder");
			}
			else if (isMethod("onFocus"))
			{
				Con::executef(this, 2, "onFocus");
			}
		}
    }
}

void GuiControl::clearFirstResponder()
{
   GuiControl *parent = this;
   while((parent = parent->getParent()) != NULL)
   {
      if(parent->mFirstResponder == this)
         parent->mFirstResponder = NULL;
      else
         break;
   }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

void GuiControl::buildAcceleratorMap()
{
   //add my own accel key
   addAcceleratorKey();

   //add all my childrens keys
   iterator i;
   for(i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      ctrl->buildAcceleratorMap();
   }
}

void GuiControl::addAcceleratorKey()
{
   //see if we have an accelerator
   if (mAcceleratorKey == StringTable->EmptyString)
      return;

   EventDescriptor accelEvent;
   ActionMap::createEventDescriptor(mAcceleratorKey, &accelEvent);

   //now we have a modifier, and a key, add them to the canvas
   GuiCanvas *root = getRoot();
   if (root)
      root->addAcceleratorKey(this, 0, accelEvent.eventCode, accelEvent.flags);
}

void GuiControl::acceleratorKeyPress(U32 index)
{
   onAction();
}

void GuiControl::acceleratorKeyRelease(U32 index)
{
   //do nothing
}

bool GuiControl::onKeyDown(const GuiEvent &event)
{
   //pass the event to the parent
   GuiControl *parent = getParent();
   if (parent)
      return parent->onKeyDown(event);
   else
      return false;
}

bool GuiControl::onKeyRepeat(const GuiEvent &event)
{
   // default to just another key down.
   return onKeyDown(event);
}

bool GuiControl::onKeyUp(const GuiEvent &event)
{
   //pass the event to the parent
   GuiControl *parent = getParent();
   if (parent)
      return parent->onKeyUp(event);
   else
      return false;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

void GuiControl::onAction()
{
   if (! mActive)
      return;

   //execute the console command
   if (mConsoleCommand && mConsoleCommand[0])
   {
      execConsoleCallback();
   }
   else
      Con::executef(this, 1, "onAction");
}

void GuiControl::onMessage(GuiControl *sender, S32 msg)
{
}

void GuiControl::messageSiblings(S32 message)
{
   GuiControl *parent = getParent();
   if (! parent) return;
   GuiControl::iterator i;
   for(i = parent->begin(); i != parent->end(); i++)
   {
      GuiControl *ctrl = dynamic_cast<GuiControl *>(*i);
      if (ctrl != this)
         ctrl->onMessage(this, message);
   }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- //

void GuiControl::onDialogPush()
{
   // Notify Script.
   if( isMethod("onDialogPush") )
      Con::executef(this, 1, "onDialogPush");

}

void GuiControl::onDialogPop()
{
   // Notify Script.
   if( isMethod("onDialogPop") )
      Con::executef(this, 1, "onDialogPop");
}

//------------------------------------------------------------------------------
void GuiControl::setVisible(bool value)
{
    mVisible = value;
   iterator i;
   setUpdate();
   for(i = begin(); i != end(); i++)
   {
      GuiControl *ctrl = static_cast<GuiControl *>(*i);
      ctrl->clearFirstResponder();
    }

    GuiControl *parent = getParent();
    if (parent)
       parent->childResized(this);
}


void GuiControl::makeFirstResponder(bool value)
{
   if ( value )
      //setFirstResponder(this);
      setFirstResponder();
   else
      clearFirstResponder();
}

void GuiControl::setActive( bool value )
{
   mActive = value;

   if (value && isMethod("onActive"))
	   Con::executef(this, 1, "onActive");
	else if (!value && isMethod("onInactive"))
		Con::executef(this, 1, "onInactive");

   if ( !mActive )
      clearFirstResponder();

   if ( mVisible && mAwake )
      setUpdate();
}

void GuiControl::getScrollLineSizes(U32 *rowHeight, U32 *columnWidth)
{
    // default to 10 pixels in y, 30 pixels in x
    *columnWidth = 30;
    *rowHeight = 30;
}

void GuiControl::renderText(const Point2I& offset, const Point2I& extent, const char* text, GuiControlProfile* profile, TextRotationOptions rot)
{
    RectI old = dglGetClipRect();
    RectI clipRect = RectI(offset, extent);
    if (clipRect.intersect(old))
    {
        dglSetClipRect(clipRect);

        const S32 textHeight = profile->getFont(mFontSizeAdjust)->getHeight();
        S32 totalWidth = (rot == tRotateNone) ? extent.x : extent.y;
        S32 totalHeight = (rot == tRotateNone) ? extent.y : extent.x;

        S32 startOffsetY = 0;

        vector<string> lineList = getLineList(text, profile, totalWidth);

        //first align vertical
        S32 blockHeight = textHeight * lineList.size();

        if (mTextExtend)
        {
            Point2I extent = getExtent();
            if (mTextWrap)
            {
                extent.y = getOuterHeight(blockHeight, NormalState, profile);
            }
            else
            {
                extent.x = getOuterWidth(profile->getFont(mFontSizeAdjust)->getStrWidth(text), NormalState, profile);
            }
            setExtent(extent);
        }

        if (blockHeight < totalHeight)
        {
            startOffsetY = getTextVerticalOffset(blockHeight, totalHeight, getVertAlignmentType(profile));
        }
        else if (!mTextWrap)
        {
            startOffsetY = getTextVerticalOffset(blockHeight, totalHeight, VertAlignmentType::MiddleVAlign);
        }
        else
        {
            startOffsetY = getTextVerticalOffset(blockHeight, totalHeight, VertAlignmentType::TopVAlign);
        }

        renderLineList(offset, extent, startOffsetY, lineList, profile, rot);
        dglSetClipRect(old);
    }
}

void GuiControl::renderLineList(const Point2I& offset, const Point2I& extent, const S32 startOffsetY, const vector<string> lineList, GuiControlProfile* profile, const TextRotationOptions rot)
{
    const S32 textHeight = profile->getFont(mFontSizeAdjust)->getHeight();
    S32 totalWidth = (rot == tRotateNone) ? extent.x : extent.y;

	//Now print each line
    U32 ibeamPos = 0;
    U32 lineNumber = 0;
    S32 offsetX = 0;
    S32 offsetY = startOffsetY;
	for(string line : lineList)
	{
		// align the horizontal
        string trimmedLine = Utility::trim_copy(line);
		U32 textWidth = profile->getFont(mFontSizeAdjust)->getStrWidth(trimmedLine.c_str());
		if(textWidth < totalWidth)
		{
            offsetX = getTextHorizontalOffset(textWidth, totalWidth, getAlignmentType(profile));
		}

		Point2I start = Point2I(0, 0);
		F32 rotation;
		if (rot == tRotateNone)
		{
            start.x += offsetX;
            start.y += offsetY;
			rotation = 0.0f;
		}
		else if (rot == tRotateLeft)
		{
			start.x = offsetY;
			start.y = extent.y + offsetX;
			rotation = 90.0f;
		}
		else if (rot == tRotateRight)
		{
			start.x = extent.x - offsetY;
			start.y = offsetX;
			rotation = -90.0f;
		}

        renderTextLine(start + offset + profile->mTextOffset, trimmedLine, profile, rotation, ibeamPos, lineNumber);
			
		offsetY += textHeight;
        ibeamPos += line.length();
        lineNumber++;
	}
}

vector<string> GuiControl::getLineList(const char* text, GuiControlProfile* profile, S32 totalWidth)
{
    GFont* font = profile->getFont(mFontSizeAdjust);
    vector<string> lineList = vector<string>();

    if (!mTextWrap)
    {
        lineList.push_back(text);
    }
    else
    {
        vector<string> paragraphList = vector<string>();
        istringstream f(text);
        string s;
        while (getline(f, s)) {
            paragraphList.push_back(s);
        }

        for (string& paragraph : paragraphList)
        {
            vector<string> wordList = vector<string>();
            istringstream f2(paragraph);
            string s2;
            while (getline(f2, s2, ' ')) {
                wordList.push_back(s2);
            }

            //now process the word list
            string line;
            bool newLine = true;
            line.clear();
            for (string& word : wordList)
            {
                if (font->getStrWidth(word.c_str()) >= totalWidth)
                {
                    if (line.size() > 0)
                    {
                        lineList.push_back(string(line + " "));
                        line.clear();
                    }
                    lineList.push_back(word + " ");
                    newLine = true;
                    continue;
                }

                string prevLine = string(line);
                line += (!newLine) ? " " + word : word;
                newLine = false;
                if (font->getStrWidth(line.c_str()) >= totalWidth && word.length() != 0)
                {
                    lineList.push_back(prevLine + " ");
                    line = word;
                }
            }
            if (paragraph.back() == ' ')
            {
                line += " ";
            }
            lineList.push_back(string(line));
        }
    }

    return lineList;
}

void GuiControl::renderTextLine(const Point2I& startPoint, const string line, GuiControlProfile* profile, F32 rotationInDegrees, U32, U32)
{
    dglDrawText(profile->getFont(mFontSizeAdjust), startPoint, line.c_str(), profile->mFontColors, 9, rotationInDegrees);
}

S32 GuiControl::getTextHorizontalOffset(S32 textWidth, S32 totalWidth, AlignmentType align)
{
	if (align == RightAlign)
	{
		return totalWidth - textWidth;
	}
	else if (align == CenterAlign)
	{
		return (totalWidth - textWidth) / 2;
	}
	return 0;//left aligned
}

S32 GuiControl::getTextVerticalOffset(S32 textHeight, S32 totalHeight, VertAlignmentType align)
{
	if (align == MiddleVAlign)
	{
		return (totalHeight - textHeight) / 2;
	}
	else if (align == BottomVAlign)
	{
		return totalHeight - textHeight;
	}
	return 0;
}

void GuiControl::getCursor(GuiCursor *&cursor, bool &showCursor, const GuiEvent &lastGuiEvent)
{
   lastGuiEvent;

   if(GuiControl::smCursorChanged != -1 && !isMouseLocked())
   {
      // We've already changed the cursor, 
      // so set it back before we change it again.
      Input::popCursor();

      // We haven't changed it
      GuiControl::smCursorChanged = -1;
   }
}

const char* GuiControl::execConsoleCallback()
{
   if (mConsoleCommand && mConsoleCommand[0])
   {
      Con::setVariable("$ThisControl", avar("%d",getId()));
      return Con::evaluate(mConsoleCommand, false);
   }
   return "";
}

const char* GuiControl::execAltConsoleCallback()
{
   if(mAltConsoleCommand && mAltConsoleCommand[0])
   {
      Con::setVariable("$ThisControl", avar("%d",getId()));
      return Con::evaluate(mAltConsoleCommand, false);
   }
   return "";
}

void GuiControl::setText(const char *text)
{
	mText = StringTable->insert(text, true);
}

void GuiControl::setTextID(const char *id)
{
	S32 n = Con::getIntVariable(id, -1);
	if (n != -1)
	{
		mTextID = StringTable->insert(id);
		setTextID(n);
	}
}
void GuiControl::setTextID(S32 id)
{
	const UTF8 *str = getGUIString(id);
	if (str)
		setText((const char*)str);
}
const char *GuiControl::getText()
{
	return mText;
}

AlignmentType GuiControl::getAlignmentType()
{
    return getAlignmentType(mProfile);
}

AlignmentType GuiControl::getAlignmentType(GuiControlProfile* profile)
{
    return mAlignment == AlignmentType::DefaultAlign ? profile->mAlignment : mAlignment;
}

VertAlignmentType GuiControl::getVertAlignmentType()
{
    return getVertAlignmentType(mProfile);
}

VertAlignmentType GuiControl::getVertAlignmentType(GuiControlProfile* profile)
{
    return mVAlignment == VertAlignmentType::DefaultVAlign ? profile->mVAlignment : mVAlignment;
}

const ColorI& GuiControl::getFontColor(GuiControlProfile* profile, const GuiControlState state)
{
    return mOverrideFontColor ? mFontColor : profile->getFontColor(state);
}