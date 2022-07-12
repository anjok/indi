/*
    OpenAstroTech
    Copyright (C) 2021 Anjo Krank

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "lx200_OpenAstroTech.h"

#include "indicom.h"
#include "lx200driver.h"

#include <cmath>
#include <string.h>

#include <libnova/transform.h>
#include <termios.h>
#include <unistd.h>
#include <mutex>

#define RB_MAX_LEN    64
#define CMD_MAX_LEN 32
extern std::mutex lx200CommsLock;

LX200_OpenAstroTech::LX200_OpenAstroTech(void) : LX200GPS()
{
    setVersion(MAJOR_VERSION, MINOR_VERSION);
}

bool LX200_OpenAstroTech::Handshake()
{
    bool result = LX200GPS::Handshake();
    return result;
}

#define OAT_MEADE_COMMAND "OAT_MEADE_COMMAND"
#define OAT_DEC_LOWER_LIMIT "OAT_DEC_LOWER_LIMIT"
#define OAT_DEC_UPPER_LIMIT "OAT_DEC_UPPER_LIMIT"
#define OAT_GET_DEBUG_LEVEL "OAT_GET_DEBUG_LEVEL"
#define OAT_GET_ENABLED_DEBUG_LEVEL "OAT_GET_ENABLED_DEBUG_LEVEL"
#define OAT_SET_DEBUG_LEVEL "OAT_GET_DEBUG_LEVEL"

const char *OAT_TAB       = "Open Astro Tech";

bool LX200_OpenAstroTech::initProperties()
{
    LX200GPS::initProperties();
    IUFillText(&MeadeCommandT, OAT_MEADE_COMMAND, "Result / Command", "");
    IUFillTextVector(&MeadeCommandTP, &MeadeCommandT, 1, getDeviceName(), "Meade", "", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);
    FI::SetCapability(FOCUSER_CAN_ABORT | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_REVERSE | FOCUSER_HAS_VARIABLE_SPEED | FOCUSER_HAS_BACKLASH);
    FI::initProperties(FOCUS_TAB);

    return true;
}

bool LX200_OpenAstroTech::updateProperties()
{
    LX200GPS::updateProperties();

    if (isConnected())
    {
        defineProperty(&MeadeCommandTP);
    }
    else
    {
        deleteProperty(MeadeCommandTP.name);
    }

    return true;
}

bool LX200_OpenAstroTech::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (!strcmp(name, MeadeCommandTP.name))
        {
            if(!isSimulation()) {
                // we're using the "Set" field for the command and the actual field for the result
                // we need to:
                // - get the value
                // - check if it's a command
                // - if so, execute it, then set the result to the control
                // the client side needs to
                // - push ":somecmd#"
                // - listen to change on MeadeCommand and log it
                char * cmd = texts[0];
                size_t len = strlen(cmd);
                DEBUGFDEVICE(getDeviceName(), DBG_SCOPE, "Meade Command <%s>", cmd);
                if(len > 2 && cmd[0] == ':' && cmd[len-1] == '#') {
                    IText *tp = IUFindText(&MeadeCommandTP, names[0]);
                    MeadeCommandResult[0] = 0;
                    int err = executeMeadeCommand(texts[0]);
                    DEBUGFDEVICE(getDeviceName(), DBG_SCOPE, "Meade Command Result %d <%s>", err, MeadeCommandResult);
                    if(err == 0) {
                        MeadeCommandTP.s = IPS_OK;
                        IUSaveText(tp, MeadeCommandResult);
                        IDSetText(&MeadeCommandTP, MeadeCommandResult);
                        return true;
                    } else {
                        MeadeCommandTP.s = IPS_ALERT;
                        IDSetText(&MeadeCommandTP, nullptr);
                        return true;
                    }
                }
            }
       }
    }

    return INDI::Telescope::ISNewText(dev, name, texts, names, n);
}

bool LX200_OpenAstroTech::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (strcmp(dev, getDeviceName()) == 0)
    {
        /* left in for later use
        if (!strcmp(name, SlewAccuracyNP.name))
        {
            if (IUUpdateNumber(&SlewAccuracyNP, values, names, n) < 0)
                return false;

            SlewAccuracyNP.s = IPS_OK;

            if (SlewAccuracyN[0].value < 3 || SlewAccuracyN[1].value < 3)
                IDSetNumber(&SlewAccuracyNP, "Warning: Setting the slew accuracy too low may result in a dead lock");

            IDSetNumber(&SlewAccuracyNP, nullptr);
            return true;
        }*/
    }

    return LX200GPS::ISNewNumber(dev, name, values, names, n);
}

bool LX200_OpenAstroTech::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    /*
    int index = 0;

    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        //Intercept Before inditelescope base can set TrackState
        //Next one modification of inditelescope.cpp function
        if (!strcmp(name, TrackStateSP.name))
        {
            //             int previousState = IUFindOnSwitchIndex(&TrackStateSP);
            IUUpdateSwitch(&TrackStateSP, states, names, n);
            int targetState = IUFindOnSwitchIndex(&TrackStateSP);
            //             LOG_DEBUG("OnStep driver TrackStateSP override called");
            //             if (previousState == targetState)
            //             {
            //                 IDSetSwitch(&TrackStateSP, nullptr);
            //                 return true;
            //             }

            if (TrackState == SCOPE_PARKED)
            {
                LOG_WARN("Telescope is Parked, Unpark before tracking.");
                return false;
            }

            bool rc = SetTrackEnabled((targetState == TRACK_ON) ? true : false);

            if (rc)
            {
                return true;
                //TrackStateSP moved to Update
            }
            else
            {
                //This is the case for an error on sending the command, so change TrackStateSP
                TrackStateSP.s = IPS_ALERT;
                IUResetSwitch(&TrackStateSP);
                return false;
            }

            LOG_DEBUG("TrackStateSP intercept, OnStep driver, should never get here");
            return false;
        }
    }
    */
    return LX200GPS::ISNewSwitch(dev, name, states, names, n);
}

const char *LX200_OpenAstroTech::getDefaultName(void)
{
    return const_cast<const char *>("LX200 OpenAstroTech");
}

int LX200_OpenAstroTech::executeMeadeCommand(const char *cmd)
{
    bool wait = true;
    int len = strlen(cmd);
    if(len > 2) {
        if(cmd[1] == 'F' && cmd[2] != 'p') { // F. except for Fp
            wait = false;
        } else if(cmd[1] == 'M'  && cmd[2] == 'A') { // MAL, MAZ
            wait = false;
        }
    }
    if(!wait) {
        return executeMeadeCommandBlind(cmd);
    }
    LOGF_INFO("Executing Meade Command: %d <%s>", wait, MeadeCommandResult);
    return getCommandString(PortFD, MeadeCommandResult, cmd);
}

bool LX200_OpenAstroTech::executeMeadeCommandBlind(const char *cmd)
{
    int error_type;
    int nbytes_write = 0;

    DEBUGF(DBG_SCOPE, "CMD <%s>", cmd);

    flushIO(PortFD);
    /* Add mutex */
    std::unique_lock<std::mutex> guard(lx200CommsLock);
    tcflush(PortFD, TCIFLUSH);

    LOGF_INFO("Executing Meade Command Immediate: <%s>", cmd);
    if ((error_type = tty_write_string(PortFD, cmd, &nbytes_write)) != TTY_OK) {
        LOGF_ERROR("CHECK CONNECTION: Error sending command %s", cmd);
        return 0; //Fail if we can't write
        //return error_type;
    }

    return 1;
}

int LX200_OpenAstroTech::flushIO(int fd)
{
    tcflush(fd, TCIOFLUSH);
    int error_type = 0;
    int nbytes_read;
    std::unique_lock<std::mutex> guard(lx200CommsLock);
    tcflush(fd, TCIOFLUSH);
    do {
        char discard_data[RB_MAX_LEN] = {0};
        error_type = tty_read_section_expanded(fd, discard_data, '#', 0, 1000, &nbytes_read);
        if (error_type >= 0) {
            LOGF_DEBUG("flushIO: Information in buffer: Bytes: %u, string: %s", nbytes_read, discard_data);
        }
        //LOGF_DEBUG("flushIO: error_type = %i", error_type);
    } while (error_type > 0);
    return 0;
}

IPState LX200_OpenAstroTech::MoveFocuser(FocusDirection dir, int speed, uint16_t duration)
{
    INDI_UNUSED(speed);
    //  :FRsnnn#  Set focuser target position relative (in microns)
    //            Returns: Nothing
    double output;
    char read_buffer[32];
    output = duration;
    if (dir == FOCUS_INWARD) output = 0 - output;
    snprintf(read_buffer, sizeof(read_buffer), ":FM%f#", output);
    executeMeadeCommandBlind(read_buffer);
    return IPS_BUSY; // Normal case, should be set to normal by update.
}

IPState LX200_OpenAstroTech::MoveAbsFocuser (uint32_t targetTicks)
{
    //  :FSsnnn#  Set focuser target position (in microns)
    //            Returns: Nothing
    if (FocusAbsPosN[0].max >= int(targetTicks) && FocusAbsPosN[0].min <= int(targetTicks))
    {
        char read_buffer[32];
        snprintf(read_buffer, sizeof(read_buffer), ":FS%06d#", int(targetTicks));
        executeMeadeCommandBlind(read_buffer);
        return IPS_BUSY; // Normal case, should be set to normal by update.
    }
    else
    {
        LOG_INFO("Unable to move focuser, out of range");
        return IPS_ALERT;
    }
}

IPState LX200_OpenAstroTech::MoveRelFocuser (FocusDirection dir, uint32_t ticks)
{
    //  :FRsnnn#  Set focuser target position relative (in microns)
    //            Returns: Nothing
    int output;
    char read_buffer[32];
    output = ticks;
    if (dir == FOCUS_INWARD) output = 0 - ticks;
    snprintf(read_buffer, sizeof(read_buffer), ":FM%d#", output);
    executeMeadeCommandBlind(read_buffer);
    return IPS_BUSY; // Normal case, should be set to normal by update.
}

bool LX200_OpenAstroTech::SetFocuserBacklash(int32_t steps)
{

    LOGF_INFO("Set backlash %d", steps);
    Backlash = steps;
    return true;
}

bool LX200_OpenAstroTech::AbortFocuser ()
{
    //  :FQ#   Stop the focuser
    //         Returns: Nothing
    char cmd[CMD_MAX_LEN] = {0};
    strncpy(cmd, ":FQ#", sizeof(cmd));
    return executeMeadeCommandBlind(cmd);
}
