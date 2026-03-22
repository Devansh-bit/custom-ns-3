This is a [Next.js](https://nextjs.org) project bootstrapped with [`create-next-app`](https://nextjs.org/docs/app/api-reference/cli/create-next-app).

## Getting Started

### Install dependencies
```bash
npm install
pip install fastapi uvicorn websockets aiokafka "psycopg[binary]" python-dotenv
```

First, run the development server:

```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

For the backend
```bash
cd api/
unicorn main:app
```
