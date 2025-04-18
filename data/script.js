document.addEventListener('DOMContentLoaded', function() {
    // 获取DOM元素
    const frequencyInput = document.getElementById('frequency');
    const txPowerInput = document.getElementById('txPower');
    const powerValueSpan = document.getElementById('powerValue');
    const txFreqDeviationInput = document.getElementById('txFreqDeviation');
    const rdsEnabledInput = document.getElementById('rdsEnabled');
    const stationNameInput = document.getElementById('stationName');
    const radioTextInput = document.getElementById('radioText');
    const monoAudioInput = document.getElementById('monoAudio');
    const preEmphTime50Input = document.getElementById('preEmphTime50');
    const saveBtn = document.getElementById('saveBtn');
    const statusMsg = document.getElementById('statusMsg');
    
    // 显示功率值
    txPowerInput.addEventListener('input', function() {
        powerValueSpan.textContent = this.value + '%';
    });
    
    // 加载当前设置
    fetch('/api/settings')
        .then(response => response.json())
        .then(data => {
            frequencyInput.value = data.frequency;
            txPowerInput.value = data.txPower;
            powerValueSpan.textContent = data.txPower + '%';
            txFreqDeviationInput.value = data.txFreqDeviation;
            rdsEnabledInput.checked = data.rdsEnabled;
            stationNameInput.value = data.stationName;
            radioTextInput.value = data.radioText;
            monoAudioInput.checked = data.monoAudio;
            preEmphTime50Input.checked = data.preEmphTime50;
        })
        .catch(error => {
            console.error('Error fetching settings:', error);
            showStatus('加载设置失败', false);
        });
    
    // 保存设置
    saveBtn.addEventListener('click', function() {
        // 验证输入
        const frequency = parseFloat(frequencyInput.value);
        if (frequency < 76 || frequency > 108) {
            showStatus('频率必须在76-108 MHz范围内', false);
            return;
        }
        
        const settings = {
            frequency: frequency,
            txPower: parseInt(txPowerInput.value),
            txFreqDeviation: parseInt(txFreqDeviationInput.value),
            rdsEnabled: rdsEnabledInput.checked,
            stationName: stationNameInput.value,
            radioText: radioTextInput.value,
            monoAudio: monoAudioInput.checked,
            preEmphTime50: preEmphTime50Input.checked
        };
        
        fetch('/api/settings', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(settings)
        })
        .then(response => {
            if (response.ok) {
                showStatus('设置已保存', true);
            } else {
                showStatus('保存设置失败', false);
            }
        })
        .catch(error => {
            console.error('Error saving settings:', error);
            showStatus('保存设置失败', false);
        });
    });
    
    // 显示状态消息
    function showStatus(message, isSuccess) {
        statusMsg.textContent = message;
        statusMsg.className = isSuccess ? 'success' : 'error';
        
        // 3秒后清除消息
        setTimeout(() => {
            statusMsg.textContent = '';
            statusMsg.className = '';
        }, 3000);
    }
});