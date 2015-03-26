# Copyright (C) 2015 Red Hat, Inc.,  Tony Asleson <tasleson@redhat.com>
# Distributed under the GNU General Public License, version 2.0.
# See: https://www.gnu.org/licenses/gpl-2.0.html
#
# Bash completion for lsmcli. This may be far from ideal, 
# suggestions & improvements appreciated!

_lsm() 
{
    local cur prev opts
    sep='#|#'
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    opts_short="-b -v -u -P -H -t -e -f -w -b"
    opts_long=" --help --version --uri --prompt --human --terse --enum \
              --force --wait --header --script "
    opts_cmds="list job-status capabilities plugin-info"

    list_args="--type"
    list_type_args="volumes pools fs snapshots exports nfs_client_auth \
                    access_groups systems disks plugins target_ports"

    opts_filter="--sys --pool --vol --disk --ag --fs --nfs"

    cap_args="--sys"

    # Check if we have a previous
    case "${prev}" in
        '--sys')
            # Is there a better way todo this?
            local items=`lsmcli list --type systems -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--pool')
            # Is there a better way todo this?
            local items=`lsmcli list --type pools -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--vol')
            # Is there a better way todo this?
            local items=`lsmcli list --type volumes -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--disk')
            # Is there a better way todo this?
            local items=`lsmcli list --type disks -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--ag')
            # Is there a better way todo this?
            local items=`lsmcli list --type access_groups -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--nfs-export')
            # Is there a better way todo this?
            local items=`lsmcli list --type exports  -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        '--tgt')
            # Is there a better way todo this?
            local items=`lsmcli list --type target_ports  -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;         
        '--fs')
            local items=`lsmcli list --type fs -t${sep} | awk -F '#' '{print $1}'`
            COMPREPLY=( $(compgen -W "${items}" -- ${cur}) )
            return 0
            ;;
        'snapshots')
            # Specific listing case where you need a fs too            
            if [[ ${COMP_WORDS[COMP_CWORD-2]} == '--type' && \
                  ${COMP_WORDS[COMP_CWORD-3]} == 'list' ]] ; then
                COMPREPLY=( $(compgen -W "--fs" -- ${cur}) )
                return 0
            fi
            ;;
        '--type')
            COMPREPLY=( $(compgen -W "${list_type_args}" -- ${cur}) )
            return 0
            ;;
        job-status)
            COMPREPLY=( $(compgen -W "--job" -- ${cur}) )
            return 0
            ;;
        list)
            COMPREPLY=( $(compgen -W "${list_args}" -- ${cur}) )
            return 0
            ;;
        capabilities)
            COMPREPLY=( $(compgen -W "${cap_args}" -- ${cur}) )
            return 0
            ;;
        *)
        ;;
    esac

    # Handle the case where we are starting out with nothing
    if [[ ${prev} == 'lsmcli' ]] ; then
        if [[ ${cur} == --* ]] ; then
            COMPREPLY=( $(compgen -W "${opts_long}"  -- ${cur}) )
            return 0
        fi

        if [[ ${cur} == -* ]] ; then
            COMPREPLY=( $(compgen -W "${opts_short}${opts_long}"  -- ${cur}) )
            return 0
        fi

        if [[ ${cur} == * ]] ; then
            COMPREPLY=( $(compgen -W "${opts_short}${opts_long}${opts_cmds}"  -- ${cur}) )
            return 0
        fi        
    fi
}
complete -F _lsm lsmcli
