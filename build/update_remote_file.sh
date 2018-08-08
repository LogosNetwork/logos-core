#!/usr/bin/env bash

function usage {
    echo "usage: ./update_remote_file.sh cluster_name [-l logos_binary_id] [-a agent_id] [-d ldb_id] [-c config_id]"
    echo "  Update one or more of logos binary, agent.py, data.ldb, and bench.json.tmpl"
    echo "  -h  | display help"
    echo "  cluster_name
      | unique name for cluster to be updated (must be already deployed through Cloudformation)"
    echo "  -l, logos_binary_id
      | unique identifier for logos_core (logos) binary version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/binaries/, containing the logos_core binary"
    echo "  -a, agent_id
      | unique identifier for agent.py version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/agents/, containing the agent.py file"
    echo "  -d, ldb_id
      | unique identifier for data.ldb version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/ldbs/, containing the data.ldb file"
    echo "  -c, config_id
      | unique identifier for bench.json.tmpl configuration template version"
    echo "      | must exist as a subdirectory inside s3://logos-bench/configs/, containing the bench.json.tmpl file"
    return 0
}

OPTIONS=l:a:d:c:h

! PARSED=$(getopt --options=${OPTIONS} --name "$0" -- "$@")
if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
    # getopt has complained about wrong arguments to stdout
    usage
    exit 2
fi

eval set -- "${PARSED}"

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

if [[ $# -ne 1 ]]; then
    echo "Must specify cluster name."
    invalidOpt=true
else
    CLUSTER_NAME=$1
    if [[ ! $(aws ec2 describe-instances --query 'Reservations[].Instances[].Tags[?Key==`Name`].Value' --output text | grep ${CLUSTER_NAME}) ]]; then
        echo "Cluster doesn't exist. Please provide a valid identifier."
        invalidOpt=true
    fi
fi

if [[ -z "$LOGOS_ID" && -z "$LDB_ID" && -z "$CONF_ID" && -z "$AGENT_ID" ]]; then
    echo "Must specify at least one file to update. "
    invalidOpt=true
fi

if [[ ${invalidOpt} = true ]]; then
    usage
    exit 3
fi

# ========================================================================================
# Done with argparse, begin bash script execution
# ========================================================================================

if [[ -n "$LOGOS_ID" ]]; then
    if [[ ! $(aws s3 ls s3://logos-bench/binaries/ | grep "PRE $LOGOS_ID/") ]]; then
        echo "logos version id does not exist. Subdirectory must be under s3://logos-bench/binaries/"
        exit 1
    fi
    aws ssm send-command --document-name "AWS-RunShellScript" \
        --targets '{"Key":"tag:aws:cloudformation:stack-name","'"Values"'":["'"$CLUSTER_NAME"'"]}' \
        --max-concurrency "100%" \
        --parameters '{"commands":["'"sudo aws s3 cp s3://logos-bench/binaries/$LOGOS_ID/logos_core /home/ubuntu/bench/logos_core"'","sudo chmod a+x /home/ubuntu/bench/logos_core"],"executionTimeout":["3600"],"workingDirectory":["/home/ubuntu/"]}' \
        --timeout-seconds 600 --region us-east-1
    if [[ $? > 0 ]]; then
        echo "SSM run command failed. Aborting."
        exit 1
    fi
fi

if [[ -n "$AGENT_ID" ]]; then
    if [[ ! $(aws s3 ls s3://logos-bench/agents/ | grep "PRE $AGENT_ID/") ]]; then
        echo "agent.py id does not exist. Subdirectory must be under s3://logos-bench/agents/"
        exit 1
    fi
    aws ssm send-command --document-name "AWS-RunShellScript" \
        --targets '{"Key":"tag:aws:cloudformation:stack-name","'"Values"'":["'"$CLUSTER_NAME"'"]}' \
        --max-concurrency "100%" \
        --parameters '{"commands":["sudo pkill -9 -f agent.py","sudo pkill -9 logos_core","'"sudo aws s3 cp s3://logos-bench/agents/$AGENT_ID/agent.py /home/ubuntu/bench/agent.py"'","sudo chmod a+x /home/ubuntu/bench/agent.py","python /home/ubuntu/bench/agent.py &"],"executionTimeout":["3600"],"workingDirectory":["/home/ubuntu/"]}' \
        --timeout-seconds 600 --region us-east-1
    if [[ $? > 0 ]]; then
        echo "SSM run command failed. Aborting."
        exit 1
    fi
fi

if [[ -n "$LDB_ID" ]]; then
    if [[ ! $(aws s3 ls s3://logos-bench/ldbs/ | grep "PRE $LDB_ID/") ]]; then
        echo "data.ldb version id does not exist. Subdirectory must be under s3://logos-bench/ldbs/"
        exit 1
    fi
    aws ssm send-command --document-name "AWS-RunShellScript" \
        --targets '{"Key":"tag:aws:cloudformation:stack-name","'"Values"'":["'"$CLUSTER_NAME"'"]}' \
        --max-concurrency "100%" \
        --parameters '{"commands":["'"sudo aws s3 cp s3://logos-bench/ldbs/$LDB_ID/data.ldb /home/ubuntu/bench/config/data.ldb"'","sudo chmod 644 /home/ubuntu/bench/config/data.ldb"],"executionTimeout":["3600"],"workingDirectory":["/home/ubuntu/"]}' \
        --timeout-seconds 600 --region us-east-1
    if [[ $? > 0 ]]; then
        echo "SSM run command failed. Aborting."
        exit 1
    fi
fi

if [[ -n "$CONF_ID" ]]; then
    if [[ ! $(aws s3 ls s3://logos-bench/configs/ | grep "PRE $CONF_ID/") ]]; then
        echo "bench.json.tmpl version id does not exist. Subdirectory must be under s3://logos-bench/configs/"
        exit 1
    fi
    aws ssm send-command --document-name "AWS-RunShellScript" \
        --targets '{"Key":"tag:aws:cloudformation:stack-name","'"Values"'":["'"$CLUSTER_NAME"'"]}' \
        --max-concurrency "100%" \
        --parameters '{"commands":["'"sudo aws s3 cp s3://logos-bench/configs/$CONF_ID/bench.json.tmpl /home/ubuntu/bench/config/bench.json.tmpl"'","sudo chmod 666 /home/ubuntu/bench/config/bench.json.tmpl","sudo /home/ubuntu/gen_config.sh"],"executionTimeout":["3600"],"workingDirectory":["/home/ubuntu/"]}' \
        --timeout-seconds 600 --region us-east-1
    if [[ $? > 0 ]]; then
        echo "SSM run command failed. Aborting."
        exit 1
    fi
fi

exit 0
