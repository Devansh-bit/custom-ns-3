import "./globals.css";
import Providers from "./providers"; // NextUI etc. (client component)
import ShellWrapper from "./ShellWrapper"; // client wrapper that conditionally applies Shell
// import { Inter } from "next/font/google"

export const metadata = {
  title: "Arista Dashboard",
  description: "AP View",
};

// const inter = Inter();

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" suppressHydrationWarning>
      <head>
        <script
          dangerouslySetInnerHTML={{
            __html: `
              (function() {
                var theme = localStorage.getItem('theme');
                if (theme === 'dark' || (!theme && window.matchMedia('(prefers-color-scheme: dark)').matches)) {
                  document.documentElement.classList.add('dark');
                }
              })();
            `,
          }}
        />
      </head>
      <body>
        <Providers>
          <ShellWrapper>{children}</ShellWrapper>
        </Providers>
      </body>
    </html>
  );
}
