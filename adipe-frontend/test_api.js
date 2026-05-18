import axios from 'axios';

async function test() {
    try {
        const res = await axios.post('http://127.0.0.1:8080/inject', {
            id: 'pkt_test123',
            urgency: 10,
            data: 'test payload',
            senderID: '0',
            signature: 0,
            destinationID: 5
        });
        console.log('Success:', res.data);
    } catch (err) {
        if (err.response) {
            console.error('Error status:', err.response.status);
            console.error('Error data:', err.response.data);
        } else {
            console.error('Error message:', err.message);
        }
    }
}
test();
