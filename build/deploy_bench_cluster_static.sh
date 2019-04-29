#!/usr/bin/env bash

function usage {
    echo "usage: ./deploy_bench_cluster_static.sh cluster_name [-l logos_binary_id] [-a agent_id] [-d ldb_id] [-c config_id] [-k key_path] [-t TTL] [-n num_nodes]"
    echo "  -h  | display help"
    echo "  cluster_name
      | unique name for cluster to be deployed through Cloudformation"
    echo "  -l, logos_binary_id
      | unique identifier for logos_core (logos) binary version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/binaries/, containing the logos_core binary"
    echo "      | if not specified, defaults to last uploaded version."
    echo "  -a, agenty_id
      | unique identifier for agent.py version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/agents/, containing the agents.py file"
    echo "      | if not specified, defaults to last uploaded version."
    echo "  -d, ldb_id
      | unique identifier for data.ldb version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/ldbs/, containing the data.ldb file"
    echo "      | if not specified, defaults to last uploaded version."
    echo "  -c, config_id
      | unique identifier for bench.json.tmpl configuration template version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/configs/, containing the bench.json.tmpl file"
    echo "      | if not specified, defaults to last uploaded version."
    echo "  -k, /path/to/pem_file
      | path to team-benchmark EC2 key pair .pem file."
    echo "      | *required*"
    echo "  -t, TTL
      | time-to-live (in minutes) for the created stack, after which it will auto-delete"
    echo "      | defaults to 120"
    echo "  -n, num_nodes
      | number of nodes in auto scaling group"
    echo "      | defaults to 4"
    return 0
}

OPTIONS=l:a:d:c:k:t:n:h

! PARSED=$(getopt --options=${OPTIONS} --name "$0" -- "$@")
if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
    # getopt has complained about wrong arguments to stdout
    usage
    exit 2
fi

eval set -- "${PARSED}"
TTL="120"

while true; do
    case "$1" in
        -l)
            LOGOS_ID="$2"
            shift 2
            ;;
        -a)
            AGENT_ID="$2"
            shift 2
            ;;
        -d)
            LDB_ID="$2"
            shift 2
            ;;
        -c)
            CONF_ID="$2"
            shift 2
            ;;
        -k)
            KEY_PATH="$2"
            shift 2
            ;;
        -t)
            TTL="$2"
            shift 2
            ;;
        -n)
            NUMBER_OF_NODES="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Programming error"
            usage
            exit 3
            ;;
    esac
done

invalidOpt=false

if [[ ! "$TTL" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid input value for TTL, must be a valid integer"
    invalidOpt=true
fi

if [[ $# -ne 1 ]]; then
    echo "Must specify valid cluster name."
    invalidOpt=true
else
    CLUSTER_NAME=$1
    if [[ $(aws ec2 describe-instances --query 'Reservations[].Instances[].Tags[?Key==`Name`].Value' --output text | grep ${CLUSTER_NAME}) ]]; then
        echo "Cluster already exists. Please use a different name."
        invalidOpt=true
    fi
fi

if [[  ! -f "$KEY_PATH"  ]]; then
    echo "Must specify valid file path to team benchmarking EC2 key pair."
    invalidOpt=true
fi


if [[ ${invalidOpt} = true ]]; then
    usage
    exit 3
fi

function get_id () {
    aws s3api list-objects --bucket "logos-bench" --prefix "$1" --query 'Contents[][].{Key: Key}' \
        | python2.7 -c "import json, sys; data=json.load(sys.stdin);\
        val=sorted(data, key=lambda x: int(x['Key'].split('/')[1].split('-')[-1]), reverse=True)[0]\
        ['Key'].split('/')[1] if data else 'false';\
        print val"
}

if [[ -z "$LOGOS_ID" ]]; then
    echo "logos binary id not specified, defaulting to last modified one."
    LOGOS_ID=$(get_id binaries)
fi
# check if bucket subdirectory actually exists
if [[ ! $(aws s3 ls s3://logos-bench/binaries/ | grep "PRE $LOGOS_ID/") ]]; then
    echo "logos version id does not exist. Subdirectory must be under s3://logos-bench/binaries/"
    exit 1
fi

if [[ -z "$AGENT_ID" ]]; then
    echo "agent.py id not specified, defaulting to last modified one."
    AGENT_ID=$(get_id agents)
fi
# check if bucket subdirectory actually exists
if [[ ! $(aws s3 ls s3://logos-bench/agents/ | grep "PRE $AGENT_ID/") ]]; then
    echo "agent.py version id does not exist. Subdirectory must be under s3://logos-bench/agents/"
    exit 1
fi

if [[ -z "$LDB_ID" ]]; then
    echo "data.ldb id not specified, defaulting to last modified one."
    LDB_ID=$(get_id ldbs)
fi
# check if bucket subdirectory actually exists
if [[ ! $(aws s3 ls s3://logos-bench/ldbs/ | grep "PRE $LDB_ID/") ]]; then
    echo "data.ldb version id does not exist. Subdirectory must be under s3://logos-bench/ldbs/"
    exit 1
fi

if [[ -z "$CONF_ID" ]]; then
    echo "bench.json.tmpl id not specified, defaulting to last modified one."
    CONF_ID=$(get_id configs)
fi
# check if bucket subdirectory actually exists
if [[ ! $(aws s3 ls s3://logos-bench/configs/ | grep "PRE $CONF_ID/") ]]; then
    echo "bench.json.tmpl id does not exist. Subdirectory must be under s3://logos-bench/configs/"
    exit 1
fi

if [[ -z "$NUMBER_OF_NODES" ]]; then
    echo "Number of nodes was not specified, defaulting to 4."
    NUMBER_OF_NODES=4
fi

# ========================================================================================
# Done with argparse, begin bash script execution
# ========================================================================================

aws cloudformation create-stack --stack-name "${CLUSTER_NAME}" \
    --capabilities CAPABILITY_IAM \
    --template-body file://./logos_benchmark_stack.json \
    --parameters \
        ParameterKey=LogosVersion,ParameterValue=${LOGOS_ID} \
        ParameterKey=AgentVersion,ParameterValue=${AGENT_ID} \
        ParameterKey=StackTTL,ParameterValue=${TTL} \
        ParameterKey=LDBVersion,ParameterValue=${LDB_ID} \
        ParameterKey=ConfVersion,ParameterValue=${CONF_ID} \
        ParameterKey=AsgMaxSize,ParameterValue=${NUMBER_OF_NODES}
sleep 2

statusList="CREATE_COMPLETE CREATE_IN_PROGRESS"
while true; do
    status=$(aws cloudformation describe-stacks --stack-name ${CLUSTER_NAME} --query Stacks[0].StackStatus)
    status=$(sed 's/\"//g' <<< ${status})
    if echo ${status} | grep "CREATE_COMPLETE" > /dev/null; then
        echo "Creation complete."
        peer_instances=$(aws ec2 describe-tags --filters "Name=resource-type,Values=instance" "Name=tag:Name,Values=$CLUSTER_NAME" --query "Tags[].ResourceId" --output text)
        peer_ips=$(aws ec2 describe-instances --instance-ids ${peer_instances} --query 'Reservations[].Instances[].NetworkInterfaces[].Association.PublicIp' --output text)
        peer_ips=$(sed 's/ /,/g' <<< ${peer_ips})
        echo ${peer_ips}
        # If in VNC viewer mode, launch new windows and ssh in
        if [[  -n "${DISPLAY}"  && ${NUMBER_OF_NODES} < 9 ]]; then
            for ip in $(echo ${peer_ips} | sed "s/,/ /g"); do
                gnome-terminal -e "ssh -o 'StrictHostKeyChecking no' -i ${KEY_PATH} ec2-user@${ip}"
            done
        fi
        exit 0
    fi
    if ! echo "$statusList" | grep -w "$status" > /dev/null; then
        echo "Unexpected status: $status."
        exit 1
    fi
    sleep 10
done